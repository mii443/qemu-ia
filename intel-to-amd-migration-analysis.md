# Intel→AMD ライブマイグレーション KVM_SET_SREGS2 エラー分析レポート

## 実行日時
2025-10-27

## エラー概要

```
[mIA] failed to ioctl KVM fd d type 4140aecd: Invalid argument
```

- **ioctl種別**: `0x4140aecd` = `KVM_SET_SREGS2`
- **エラーコード**: `EINVAL` (Invalid argument)
- **発生タイミング**: IntelホストからAMDホストへのライブマイグレーション後

---

## エラー時のレジスタ状態

### セグメントレジスタ

```
CS: base=0000000000000000 limit=ffffffff sel=0010 type=0b p=1 dpl=0 db=0 s=1 l=1 g=1 avl=0 unusable=0
DS: base=0000000000000000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
ES: base=0000000000000000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
FS: base=0000000000000000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
GS: base=ffffa0e61489f000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
SS: base=0000000000000000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
TR: base=fffffe23a9c68000 limit=00004087 sel=0040 type=0b p=1 dpl=0 db=0 s=0 l=0 g=0 avl=0 unusable=0
LDT: base=0000000000000000 limit=ffffffff sel=0000 type=00 p=0 dpl=0 db=1 s=0 l=0 g=1 avl=0 unusable=1
```

**分析**:
- **CS**: Long mode (l=1), コードセグメント正常
- **TR type=0x0b**: Busy 64-bit TSS（Long modeで正常）
- **データセグメント**: 全てunusable=1（Long modeで正常）

### ディスクリプタテーブル

```
GDT: base=fffffe23a9c66000 limit=007f
IDT: base=fffffe0000000000 limit=0fff
```

### 制御レジスタ

```
CR0 = 0x80050033
CR2 = 0xffb79afc
CR3 = 0x029aa002
CR4 = 0x130ef0
CR8 = 0x01
```

#### CR4 詳細分析 (0x130ef0)

| ビット | 機能 | 状態 | 説明 |
|--------|------|------|------|
| 4 | PSE | 1 | Page Size Extension |
| 5 | PAE | 1 | Physical Address Extension |
| 6 | MCE | 1 | Machine Check Exception |
| 7 | PGE | 1 | Page Global Enable |
| 9 | OSFXSR | 1 | OS Support for FXSAVE/FXRSTOR |
| 10 | OSXMMEXCPT | 1 | OS Support for Unmasked SIMD FP Exceptions |
| 11 | UMIP | 1 | User-Mode Instruction Prevention |
| 16 | FSGSBASE | 1 | FS/GS base address instructions |
| **17** | **PCIDE** | **1** | **Process-Context Identifiers Enable** |
| 20 | SMEP | 1 | Supervisor Mode Execution Prevention |

#### CR3 詳細分析 (0x029aa002)

```
Physical Address: 0x0000029aa000
PCID (CR3[11:0]): 0x002
```

**重要**: CR4.PCIDE=1 のため、CR3の下位12ビットがPCID（Process-Context ID = 2）として解釈される。

### EFER レジスタ

```
EFER = 0xd01
```

| ビット | 機能 | 状態 | 説明 |
|--------|------|------|------|
| 0 | SCE | 1 | System Call Extensions |
| 8 | LME | 1 | Long Mode Enable |
| 10 | LMA | 1 | Long Mode Active |
| 11 | NXE | 1 | No-Execute Enable |
| **12** | **SVME** | **0** | **Secure Virtual Machine Enable (AMD only)** |

**重要**: SVME=0 は正常（ゲストVM内でネストされた仮想化は使用していない）

### APIC/その他

```
APIC_BASE = 0xfee00d00
flags = 0x0000000000000000
```

### PDPTRs (Page Directory Pointer Table Registers)

```
PDPTR[0] = 0x0000000000000000
PDPTR[1] = 0x0000000000000000
PDPTR[2] = 0x0000000000000000
PDPTR[3] = 0x0000000000000000
```

**問題の核心**: PDPTRsが全て0

---

## ホスト環境

### ターゲットホスト (AMD)

```
CPU: AMD Ryzen 9 7950X3D 16-Core Processor
KVMモジュール: kvm_amd
```

**サポートされている機能** (CR4関連):
- fsgsbase: ✓ サポート
- smep: ✓ サポート
- pcid: ✓ サポート
- umip: ✓ サポート

**結論**: AMDホストはCR4で使用されている全ての機能をサポートしている。
ハードウェア互換性の問題ではない。

---

## 根本原因分析

### 原因1: PDPTRs検証エラー（最有力）

#### 問題の流れ

1. **マイグレーション時**:
   - `target/i386/machine.c:1504` の `pdptrs_post_load()` が呼ばれる
   - この関数は無条件で `env->pdptrs_valid = true` を設定

   ```c
   static int pdptrs_post_load(void *opaque, int version_id)
   {
       X86CPU *cpu = opaque;
       CPUX86State *env = &cpu->env;
       env->pdptrs_valid = true;  // ← 無条件にtrue
       return 0;
   }
   ```

2. **KVM_SET_SREGS2実行時**:
   - `target/i386/kvm/kvm.c:3599-3603` でPDPTRsを設定

   ```c
   if (env->pdptrs_valid) {
       for (i = 0; i < 4; i++) {
           sregs.pdptrs[i] = env->pdptrs[i];  // ← 全て0
       }
       sregs.flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;  // ← validフラグを設定
   }
   ```

3. **カーネルKVMドライバでの検証**:
   - `KVM_SREGS2_FLAGS_PDPTRS_VALID` がセットされている
   - しかしPDPTRs全てが0
   - CR4.PAE=1 の状態でこの組み合わせは無効
   - → **EINVAL** を返す

#### Intel SDM (Software Developer's Manual) によると

Long modeかつCR4.PAE=1の場合:
- CR4.PCIDE=0: PDPTRsはCR3から自動的にロードされる
- CR4.PCIDE=1: PDPTRsは使用されない（4-level pagingが強制される）

現在の状態:
- CR4.PAE=1
- CR4.PCIDE=1
- pdptrs_valid=true ← **これが問題**
- PDPTRs=0

**結論**: Long mode + PCIDE=1 の場合、PDPTRsは意味を持たないため、
`pdptrs_valid=false` であるべき。

### 原因2: CR4.PCIDE と CR3 の整合性（副次的）

- CR4.PCIDE=1 かつ CR3[11:0]=0x002 (PCID=2)
- この組み合わせ自体は有効
- しかし、pdptrs_valid=true と組み合わさると、AMD KVMドライバの
  検証ロジックが厳格になり、拒否する可能性がある

---

## 関連コードの位置

### QEMU コード

| ファイル | 行番号 | 説明 |
|---------|--------|------|
| `target/i386/machine.c` | 1504-1510 | `pdptrs_post_load()` - 問題の関数 |
| `target/i386/kvm/kvm.c` | 3599-3604 | `kvm_put_sregs2()` - PDPTRsのセット |
| `target/i386/kvm/kvm.c` | 5300 | `kvm_arch_put_registers()` - sregs2の呼び出し |
| `accel/kvm/kvm-all.c` | 3406 | エラーメッセージの出力箇所 |

### Linux Kernel KVM

- `arch/x86/kvm/x86.c`: `__set_sregs2()` - メイン処理
- `arch/x86/kvm/x86.c`: `kvm_is_valid_cr4()` - CR4検証
- `arch/x86/kvm/x86.c`: `__kvm_valid_efer()` - EFER検証
- AMD固有: `arch/x86/kvm/svm/svm.c` - vendor-specific callbacks

---

## 解決策

### 解決策1: pdptrs_post_load の修正（推奨）

**ファイル**: `target/i386/machine.c`

**現在のコード** (1504-1510行目):
```c
static int pdptrs_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;
    env->pdptrs_valid = true;
    return 0;
}
```

**修正後のコード**:
```c
static int pdptrs_post_load(void *opaque, int version_id)
{
    X86CPU *cpu = opaque;
    CPUX86State *env = &cpu->env;

    /*
     * PDPTRs are only valid in PAE paging mode when PCIDE is disabled.
     * In long mode with PCIDE=1, 4-level paging is used and PDPTRs
     * are not applicable.
     *
     * Only set pdptrs_valid if at least one PDPTR is non-zero,
     * indicating they were actually saved with valid values.
     */
    if (env->pdptrs[0] == 0 && env->pdptrs[1] == 0 &&
        env->pdptrs[2] == 0 && env->pdptrs[3] == 0) {
        env->pdptrs_valid = false;
    } else {
        env->pdptrs_valid = true;
    }

    return 0;
}
```

**理由**:
- PDPTRsが全て0の場合は有効なデータではない
- Long mode + PCIDE=1 では4-level pagingが使われ、PDPTRsは不要
- この修正により、不要なPDPTRs検証エラーを回避できる

### 解決策2: kvm_put_sregs2 での追加チェック（代替案）

**ファイル**: `target/i386/kvm/kvm.c`

**現在のコード** (3599-3604行目):
```c
if (env->pdptrs_valid) {
    for (i = 0; i < 4; i++) {
        sregs.pdptrs[i] = env->pdptrs[i];
    }
    sregs.flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;
}
```

**修正後のコード**:
```c
if (env->pdptrs_valid) {
    bool has_nonzero_pdptr = false;

    for (i = 0; i < 4; i++) {
        sregs.pdptrs[i] = env->pdptrs[i];
        if (env->pdptrs[i] != 0) {
            has_nonzero_pdptr = true;
        }
    }

    /*
     * Only set PDPTRS_VALID flag if at least one PDPTR is non-zero.
     * This avoids validation errors in KVM when migrating to AMD hosts
     * with long mode + PCIDE=1, where PDPTRs are not used.
     */
    if (has_nonzero_pdptr) {
        sregs.flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;
    }
}
```

**理由**:
- KVMに送る直前で最終チェックを行う
- PDPTRsが全て0の場合はvalidフラグをセットしない
- より防御的なアプローチ

### 解決策3: 両方を組み合わせる（最も堅牢）

解決策1と解決策2の両方を実装することで、最も堅牢な解決策となる。

---

## 検証手順

### 1. 修正前の状態確認

エラーメッセージから以下を確認:
```
PDPTRs: 0000000000000000 0000000000000000 0000000000000000 0000000000000000
```

### 2. デバッグログの追加（オプション）

`target/i386/machine.c` の `pdptrs_post_load()` に:
```c
printf("[DEBUG] PDPTRs after load: %016llx %016llx %016llx %016llx\n",
       env->pdptrs[0], env->pdptrs[1], env->pdptrs[2], env->pdptrs[3]);
printf("[DEBUG] pdptrs_valid = %s\n", env->pdptrs_valid ? "true" : "false");
```

`target/i386/kvm/kvm.c` の `kvm_put_sregs2()` に:
```c
printf("[DEBUG] Calling KVM_SET_SREGS2 with flags=0x%llx\n", sregs.flags);
printf("[DEBUG] PDPTRS_VALID flag = %s\n",
       (sregs.flags & KVM_SREGS2_FLAGS_PDPTRS_VALID) ? "SET" : "NOT SET");
```

### 3. 修正後のテスト

1. QEMUを再ビルド
   ```bash
   cd /home/mii/work/qemu-ia
   make -j$(nproc)
   ```

2. Intel→AMDマイグレーションを実行

3. 期待される動作:
   - `KVM_SET_SREGS2` が成功（EINVAL エラーが出ない）
   - デバッグログで `pdptrs_valid = false` が確認できる
   - ゲストOSが正常に動作を継続する

---

## 追加調査が必要な場合

### カーネルKVMのログを有効化

```bash
# KVM trace pointsを有効化
echo 1 | sudo tee /sys/kernel/debug/tracing/events/kvm/enable

# エラー詳細を確認
sudo dmesg -w
```

### QEMUのKVMトレースを有効化

```bash
qemu-system-x86_64 ... -trace 'kvm_*' -D /tmp/qemu-trace.log
```

---

## 参考資料

### Intel Software Developer's Manual

- Volume 3A, Chapter 4: Paging
  - 4.4.1: PDPTE Registers
  - 4.5: 4-Level Paging and 5-Level Paging
  - 4.10.4: Process-Context Identifiers (PCIDs)

### Linux Kernel Documentation

- `Documentation/virt/kvm/api.rst`: KVM_SET_SREGS2 API
- `arch/x86/kvm/x86.c`: KVM sregs validation logic

### QEMU Documentation

- `docs/devel/migration.rst`: Migration framework
- `docs/system/i386/cpu.rst`: x86 CPU features

---

## 結論

IntelからAMDへのライブマイグレーション時に発生する `KVM_SET_SREGS2` エラーは、
**PDPTRs検証の不整合**が原因である可能性が最も高い。

具体的には:
1. Long mode + CR4.PCIDE=1 の状態ではPDPTRsは使用されない
2. しかし `pdptrs_valid=true` が無条件に設定されている
3. PDPTRs全てが0の状態で `PDPTRS_VALID` フラグをセットすると、
   AMDのKVMドライバが検証エラーを返す

**推奨される修正**:
- `target/i386/machine.c` の `pdptrs_post_load()` 関数を修正し、
  PDPTRsが全て0の場合は `pdptrs_valid=false` を設定する

この修正により、IntelからAMDへのクロスベンダーマイグレーションが
正常に動作することが期待される。

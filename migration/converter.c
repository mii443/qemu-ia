/*
 * CPU bendor state converter
 *
 * Copyright (C) 2006-2008 Qumranet Technologies
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Masato Imai         <mii@sfc.wide.ad.jp>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "converter.h"
#include <stdio.h>

void convert_vmstate_intel_to_amd(void) {
    printf("[mIA] Converting VM state from Intel to AMD...\n");
/*    CPUState *cpu;

    CPU_FOREACH(cpu) {
        X86CPU *x86_cpu = X86_CPU(cpu);
         Convert SREGS
        CPUX86State *env = &x86_cpu->env;
        env->cr[4] &= ~0x2000;
//            env->nested_state->format = 1;
    }*/
}

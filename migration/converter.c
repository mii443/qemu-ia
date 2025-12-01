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

#include "qemu/osdep.h"
#include "converter.h"

/*
 * Weak stub for targets that don't implement vendor conversion.
 * Target-specific implementations (e.g., x86) will override this.
 */
void __attribute__((weak)) convert_vmstate_intel_to_amd(void)
{
    /* No conversion needed for non-x86 targets */
}

void __attribute__((weak)) convert_vmstate_amd_to_intel(void)
{
    /* No conversion needed for non-x86 targets */
}

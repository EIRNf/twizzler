/*
 * SPDX-FileCopyrightText: 2021 Daniel Bittman <danielbittman1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <twz/sys/sys.h>

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

#include <twz/debug.h>
long linux_sys_arch_prctl(int code, unsigned long addr)
{
	switch(code) {
		case ARCH_SET_FS:
			return sys_thrd_ctl(THRD_CTL_SET_FS, (long)addr);
			break;
		case ARCH_SET_GS:
			return sys_thrd_ctl(THRD_CTL_SET_GS, (long)addr);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

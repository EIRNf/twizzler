/*
 * SPDX-FileCopyrightText: 2021 Daniel Bittman <danielbittman1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "syscalls.h"
#include <errno.h>
#include <string.h>
#include <twz/sys/obj.h>
#include <twz/sys/slots.h>
#include <twz/sys/thread.h>
#include <twz/sys/view.h>

static twzobj unix_obj;
static bool unix_obj_init = false;
static struct unix_repr *uh;

void __linux_init(void)
{
	__fd_sys_init();
	if(!unix_obj_init) {
		unix_obj_init = true;
		uint32_t fl;
		twz_view_get(NULL, TWZSLOT_UNIX, NULL, &fl);
		if(!(fl & VE_VALID)) {
			objid_t id;
			if(twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE, 0, 0, &id)) {
				twix_panic("failed to create unix object");
			}
			twz_view_set(NULL, TWZSLOT_UNIX, id, VE_READ | VE_WRITE);
			twz_object_init_ptr(&unix_obj, SLOT_TO_VADDR(TWZSLOT_UNIX));

			uh = twz_object_base(&unix_obj);
			uh->pid = 1;
			uh->tid = 1;
		} else {
			twz_object_init_ptr(&unix_obj, SLOT_TO_VADDR(TWZSLOT_UNIX));
		}

		uh = twz_object_base(&unix_obj);
		// linux_sys_write(2, "", 0);
	}
}

#include <stdarg.h>
#include <stdio.h>
__attribute__((noreturn)) void twix_panic(const char *s, ...)
{
	va_list ap;
	va_start(ap, s);
	vfprintf(stderr, s, ap);
	va_end(ap);
	twz_thread_exit(~0ul);
}

#include <sys/utsname.h>
long linux_sys_uname(struct utsname *u)
{
	strcpy(u->sysname, "Twizzler");
	strcpy(u->nodename, "twizzler"); // TODO
	strcpy(u->release, "0.1");
	strcpy(u->version, "0.1");
	strcpy(u->machine, "x86_64");
	return 0;
}

long linux_sys_getuid(void)
{
	return uh ? uh->uid : -ENOSYS;
}

long linux_sys_getgid(void)
{
	return uh ? uh->gid : -ENOSYS;
}

long linux_sys_geteuid(void)
{
	return uh ? uh->euid : -ENOSYS;
}

long linux_sys_getegid(void)
{
	return uh ? uh->egid : -ENOSYS;
}

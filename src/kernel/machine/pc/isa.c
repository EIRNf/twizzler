/*
 * SPDX-FileCopyrightText: 2021 Daniel Bittman <danielbittman1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <device.h>
#include <machine/isa.h>
#include <spinlock.h>

static struct object *pc_isa_bus;
static struct spinlock lock = SPINLOCK_INIT;
static _Atomic bool init = false;

struct object *pc_get_isa_bus(void)
{
	if(!init) {
		spinlock_acquire_save(&lock);
		if(!init) {
			/* krc: move */
			pc_isa_bus = bus_register(DEVICE_BT_ISA, 0, 0);
			kso_setname(pc_isa_bus, "ISA Bus");
			kso_root_attach(pc_isa_bus, 0, KSO_DEVBUS);
			init = true;
		}
		spinlock_release_restore(&lock);
	}
	return pc_isa_bus;
}

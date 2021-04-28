#include <stdio.h>
#include <twz/meta.h>
#include <twz/obj.h>
#include <twz/sys/dev/bus.h>
#include <twz/sys/dev/device.h>
#include <twz/sys/dev/processor.h>
#include <twz/sys/dev/system.h>
#include <twz/sys/kso.h>

void print_system(twzobj *sys)
{
	struct bus_repr *rep = twz_bus_getrepr(sys);
	for(size_t i = 0; i < rep->max_children; i++) {
		twzobj cpu;
		twz_bus_open_child(sys, &cpu, i, FE_READ);

		struct device_repr *dr = twz_device_getrepr(&cpu);
		struct processor_header *ph = twz_device_getds(&cpu);
		if((dr->device_id >> 24) == 0) {
			/* is CPU */
			printf("CPU %d\n", dr->device_id);

			printf("  thr_switch : %-ld\n", ph->stats.thr_switch);
			printf("  ext_intr   : %-ld\n", ph->stats.ext_intr);
			printf("  int_intr   : %-ld\n", ph->stats.int_intr);
			printf("  running    : %-ld\n", ph->stats.running);
			printf("  sctx_switch: %-ld\n", ph->stats.sctx_switch);
			printf("  shootdowns : %-ld\n", ph->stats.shootdowns);
			printf("  syscalls   : %-ld\n", ph->stats.syscalls);
		}
	}
}

int main()
{
	twzobj root, bus;
	twz_object_init_guid(&root, 1, FE_READ);

	struct kso_root_repr *r = twz_object_base(&root);
	for(size_t i = 0; i < r->count; i++) {
		struct kso_attachment *k = &r->attached[i];
		if(!k->id || !k->type)
			continue;
		switch(k->type) {
			case KSO_DEVBUS:
				twz_object_init_guid(&bus, k->id, FE_READ);
				struct bus_repr *rep = twz_bus_getrepr(&bus);
				if(rep->bus_type == DEVICE_BT_SYSTEM) {
					print_system(&bus);
				}
				break;
		}
	}
	return 0;
}

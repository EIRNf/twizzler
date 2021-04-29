#include <stdlib.h>
#include <sys/wait.h>
#include <twz/_err.h>
#include <twz/debug.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/obj/bstream.h>
#include <twz/sys/dev/bus.h>
#include <twz/sys/dev/device.h>
#include <twz/sys/dev/system.h>
#include <twz/sys/thread.h>
#include <unistd.h>

const char *kso_names[] = {
	[KSO_ROOT] = "root",
	[KSO_THREAD] = "thread",
	[KSO_VIEW] = "view",
	[KSO_SECCTX] = "secctx",
	[KSO_DEVBUS] = "devbus",
	[KSO_DEVICE] = "device",
};

void print_kat(struct kso_attachment *k, int indent)
{
	printf("%*s", indent, "");
	if(k->type >= KSO_MAX) {
		printf("[unknown] ");
	} else {
		printf("[%s] ", kso_names[k->type]);
	}

	twzobj obj;
	twz_object_init_guid(&obj, k->id, FE_READ);
	struct kso_hdr *hdr = twz_object_base(&obj);

	printf("%s", hdr->name);
	printf("\n");
}

void kls_devbus(struct kso_attachment *p, int indent)
{
	twzobj bus;
	twz_object_init_guid(&bus, p->id, FE_READ);
	struct bus_repr *r = twz_bus_getrepr(&bus);
	if(r->bus_type == DEVICE_BT_SYSTEM) {
		struct system_header *sh = twz_bus_getbs(&bus);
		printf("%*snrcpus: %ld\n", indent, "", sh->nrcpus);
		printf("%*spagesz: %ld\n", indent, "", sh->pagesz);
	}
	for(size_t i = 0; i < r->max_children; i++) {
		struct kso_attachment *k = twz_object_lea(&bus, &r->children[i]);
		if(k->id == 0)
			continue;
		print_kat(k, indent);
	}
}

void kls_thread(struct kso_attachment *p, int indent)
{
	twzobj thr;
	twz_object_init_guid(&thr, p->id, FE_READ);
	struct twzthread_repr *r = twz_object_base(&thr);
	for(int i = 0; i < TWZ_THRD_MAX_SCS; i++) {
		struct kso_attachment *k = &r->attached[i];
		if(k->id == 0 || k->type != KSO_SECCTX)
			continue;
		print_kat(k, indent);
	}
}

#include <twz/twztry.h>
void kls(void)
{
	twzobj root;
	twz_object_init_guid(&root, 1, FE_READ);

	struct kso_root_repr *r = twz_object_base(&root);
	for(size_t i = 0; i < r->count; i++) {
		struct kso_attachment *k = &r->attached[i];
		if(!k->id || !k->type)
			continue;
		twztry
		{
			print_kat(k, 0);
			switch(k->type) {
				case KSO_THREAD:
					kls_thread(k, 4);
					break;
				case KSO_DEVBUS:
					kls_devbus(k, 4);
			}
		}
		twzcatch_all
		{
		}
		twztry_end;
	}
}

int main()
{
	kls();
	return 0;
}

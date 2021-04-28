#include <arena.h>
#include <clksrc.h>
#include <debug.h>
#include <device.h>
#include <init.h>
#include <memory.h>
#include <object.h>
#include <page.h>
#include <processor.h>
#include <secctx.h>
#include <thread.h>
#include <time.h>
#include <twz/sys/dev/bus.h>
#include <twz/sys/dev/device.h>
#include <twz/sys/dev/system.h>

#include <twz/meta.h>
#include <twz/objid.h>
#include <twz/sys/slots.h>
#include <twz/sys/thread.h>

struct object *get_system_object(void)
{
	static struct object *system_bus;
	static struct spinlock lock = SPINLOCK_INIT;
	static _Atomic bool init = false;

	if(!init) {
		spinlock_acquire_save(&lock);
		if(!init) {
			system_bus = bus_register(DEVICE_BT_SYSTEM, 0, sizeof(struct system_header));
			kso_setname(system_bus, "System");
			kso_root_attach(system_bus, 0, KSO_DEVBUS);
			init = true;
		}
		spinlock_release_restore(&lock);
	}
	return system_bus; /* krc: move */
}

static struct arena post_init_call_arena;
static struct init_call *post_init_call_head = NULL;

void post_init_call_register(bool ac, void (*fn)(void *), void *data)
{
	if(post_init_call_head == NULL) {
		arena_create(&post_init_call_arena);
	}

	struct init_call *ic = arena_allocate(&post_init_call_arena, sizeof(struct init_call));
	ic->fn = fn;
	ic->data = data;
	ic->allcpus = ac;
	ic->next = post_init_call_head;
	post_init_call_head = ic;
}

static void post_init_calls_execute(bool secondary)
{
	for(struct init_call *call = post_init_call_head; call != NULL; call = call->next) {
		if(!secondary || call->allcpus) {
			call->fn(call->data);
		}
	}
}

/* functions called from here expect virtual memory to be set up. However, functions
 * called from here cannot rely on global contructors having run, as those are allowed
 * to use memory management routines, so they are run after this. Furthermore,
 * they cannot use per-cpu data.
 */
void kernel_early_init(void)
{
	mm_init();
	processor_percpu_regions_init();
	processor_early_init();
}

/* at this point, memory management, interrupt routines, global constructors, and shared
 * kernel state between nodes have been initialized. Now initialize all application processors
 * and per-node threading.
 */

extern void _init(void);
extern int kernel_init_array_start;
extern int kernel_init_array_end;
void kernel_init(void)
{
	page_init_bootstrap();
	mm_init_phase_2();
	uint64_t *init_arr = (uint64_t *)&kernel_init_array_start;
	uint64_t *init_arr_stop = (uint64_t *)&kernel_init_array_end;
	while(init_arr != init_arr_stop) {
		// printk(":::: %p\n", *init_arr);
		if(*init_arr) {
			void (*fn)() = (void (*)()) * init_arr;
			fn();
		}
		init_arr++;
	}
	// printk(":::: %p\n", *init_arr);
	//_init();
	processor_init_secondaries();
}

#if 0
static void bench(void)
{
	printk("Starting benchmark\n");
	arch_interrupt_set(true);
	return;
	int c = 0;
	for(c = 0; c < 5; c++) {
		// uint64_t sr = rdtsc();
		// uint64_t start = clksrc_get_nanoseconds();
		// uint64_t end = clksrc_get_nanoseconds();
		// uint64_t er = rdtsc();
		// printk(":: %ld %ld\n", end - start, er - sr);
		// printk(":: %ld\n", er - sr);

#if 0
		uint64_t start = clksrc_get_nanoseconds();
		volatile int i;
		uint64_t c = 0;
		int64_t max = 1000000000;
		for(i = 0; i < max; i++) {
			volatile int x = i ^ (i - 1);
			//	uint64_t x = rdtsc();
			// clksrc_get_nanoseconds();
			//	uint64_t y = rdtsc();
			//	c += (y - x);
		}
		uint64_t end = clksrc_get_nanoseconds();
		printk("Done: %ld (%ld)\n", end - start, (end - start) / i);
		// printk("RD: %ld (%ld)\n", c, c / i);
		start = clksrc_get_nanoseconds();
		for(i = 0; i < max; i++) {
			us1[i % 0x1000] = i & 0xff;
		}
		end = clksrc_get_nanoseconds();
		printk("MEMD: %ld (%ld)\n", end - start, (end - start) / i);
#else
		while(true) {
			uint64_t t = clksrc_get_nanoseconds();
			if(((t / 1000000) % 1000) == 0)
				printk("ONE SECOND %ld\n", t);
		}
		uint64_t start = clksrc_get_nanoseconds();
		// for(long i=0;i<800000000l;i++);
		for(long i = 0; i < 800000000l; i++)
			;
		uint64_t end = clksrc_get_nanoseconds();
		printk("Done: %ld\n", end - start);
		if(c++ == 10)
			panic("reset");
#endif
	}
	for(;;)
		;
}
#endif

static _Atomic unsigned int kernel_main_barrier = 0;

#include <kc.h>
#include <object.h>

struct elf64_header {
	uint8_t e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};
typedef struct {
	uint32_t p_type;   /* Segment type */
	uint32_t p_flags;  /* Segment flags */
	uint64_t p_offset; /* Segment file offset */
	uint64_t p_vaddr;  /* Segment virtual address */
	uint64_t p_paddr;  /* Segment physical address */
	uint32_t p_filesz; /* Segment size in file */
	uint32_t p_memsz;  /* Segment size in memory */
	uint32_t p_align;  /* Segment alignment */
} Elf64_Phdr;

static __inline__ unsigned long long rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

#include <syscall.h>
void kernel_main(struct processor *proc)
{
	if(proc->flags & PROCESSOR_BSP) {
		/* create the root object. TODO: load an old one? */
		struct object *root = obj_create(KSO_ROOT_ID, KSO_ROOT);
		struct metainfo mi = {
			.p_flags = MIP_DFL_READ,
			.flags = 0,
			.milen = sizeof(mi) + 128,
			.kuid = 0,
			.nonce = 0,
			.magic = MI_MAGIC,
		};
		root->flags |= OF_IDSAFE | OF_IDCACHED;

		obj_write_data(
		  root, OBJ_MAXSIZE - (OBJ_NULLPAGE_SIZE + OBJ_METAPAGE_SIZE), sizeof(mi), &mi);
		struct object *so = get_system_object();
		struct system_header *hdr = bus_get_busspecific(so);
		hdr->pagesz = mm_page_size(0);
		device_release_headers(so);

		obj_put(so);
		printk("[kernel] sizeof struct page: %ld\n", sizeof(struct page));
	}
	post_init_calls_execute(!(proc->flags & PROCESSOR_BSP));

	// printk("Waiting at kernel_main_barrier\n");
	processor_barrier(&kernel_main_barrier);

	if(proc->flags & PROCESSOR_BSP) {
		arena_destroy(&post_init_call_arena);
		post_init_call_head = NULL;

		// bench();
		// if(kc_bsv_id == 0) {
		//	panic("No bsv specified");
		//}
		if(kc_init_id == 0) {
			panic("No init specified");
		}

		struct object *initobj = obj_lookup(kc_init_id, 0);
		if(!initobj) {
			panic("Cannot load init object");
		}

		objid_t dataid;
		int r;
		r = syscall_ocreate(0, 0, 0, 0, MIP_DFL_READ | MIP_DFL_WRITE, &dataid);
		if(r < 0)
			panic("failed to create initial objects: %d", r);
		struct object *dataobj = obj_lookup(dataid, 0);

		struct elf64_header elf;
		/* this object is almost certainly not a KSO, so there's no point holding a reference to the
		 * kaddr. */
		obj_read_data(initobj, 0, sizeof(elf), &elf);
		if(memcmp("\x7F"
		          "ELF",
		     elf.e_ident,
		     4)) {
			panic("bootstrap init is not an ELF file");
		}

		for(int i = 0; i < elf.e_phnum; i++) {
			uint64_t _data[elf.e_phentsize];
			Elf64_Phdr *p = (void *)_data;
			obj_read_data(
			  initobj, elf.e_phoff + i * elf.e_phentsize, elf.e_phentsize, (void *)_data);
			if(p->p_type == 1 && p->p_vaddr == 0x40001000) {
				size_t len = p->p_filesz;

				char buf[4096];
				for(size_t x = 0; x < len;) {
					size_t this = 0x1000;
					if(this + x > len)
						this = len - x;
					obj_read_data(initobj, p->p_offset + x, this, buf);
					obj_write_data(dataobj, x, this, buf);
					x += this;
				}
			}
		}

		obj_put(dataobj);
		obj_put(initobj);

#define US_STACK_SIZE 0x200000 - 0x1000
		char *stck_obj = (void *)(0x400040000000ull);
		char *thrd_obj = (void *)(0x400000000000ull);
		size_t off = US_STACK_SIZE - 0x100, tmp = 0;

		//	printk("stck slot = %ld\n", (uintptr_t)stck_obj / mm_page_size(MAX_PGLEVEL));
		//	printk("thrd slot = %ld\n", (uintptr_t)thrd_obj / mm_page_size(MAX_PGLEVEL));

		char name_id[64];
		snprintf(name_id, 64, "BSNAME=" IDFMT, IDPR(kc_name_id));

		long vector[6] = {
			[0] = 1, /* argc */
			[1] = 0, /* argv[0] */
			[2] = 0, /* argv[1] == NULL */
			[3] = 0, /* envp[0] == NAME */
			[4] = 0, /* envp[1] == NULL */
			[5] = 0, /* envp[2] == NULL */
			         //[1] = (long)thrd_obj + off + sizeof(vector) + 0x1000,
		};

		objid_t bthrid;
		objid_t bstckid;
		objid_t bsvid;

		r = syscall_ocreate(0, 0, 0, 0, MIP_DFL_READ | MIP_DFL_WRITE, &bthrid);
		if(r < 0)
			panic("failed to create initial objects: %d", r);
		r = syscall_ocreate(0, 0, 0, 0, MIP_DFL_READ | MIP_DFL_WRITE, &bstckid);
		if(r < 0)
			panic("failed to create initial objects: %d", r);
		r = syscall_ocreate(0, 0, 0, 0, MIP_DFL_READ | MIP_DFL_WRITE, &bsvid);
		if(r < 0)
			panic("failed to create initial objects: %d", r);
		/*	printk("Created bthrd = " IDFMT ", bstck = " IDFMT ", bsvid = " IDFMT "\n",
		      IDPR(bthrid),
		      IDPR(bstckid),
		      IDPR(bsvid));*/

		struct object *bthr = obj_lookup(bthrid, 0);
		struct object *bstck = obj_lookup(bstckid, 0);
		struct object *bv = obj_lookup(bsvid, 0);
		assert(bthr && bstck && bv);

		struct viewentry v_s = {
			.id = bstckid,
			.flags = VE_READ | VE_WRITE | VE_VALID,
		};

		struct viewentry v_v = {
			.id = bsvid,
			.flags = VE_READ | VE_WRITE | VE_VALID,
		};

		kso_view_write(bv, TWZSLOT_CVIEW, &v_v);

		struct viewentry v_i = {
			.id = kc_init_id,
			.flags = VE_READ | VE_EXEC | VE_VALID,
		};

		kso_view_write(bv, 0, &v_i);
		struct viewentry v_d = {
			.id = dataid,
			.flags = VE_READ | VE_WRITE | VE_VALID,
		};

		kso_view_write(bv, 1, &v_d);

		kso_view_write(bv, TWZSLOT_STACK, &v_s);

		char *init_argv0 = "___init";
		obj_write_data(bstck, off, strlen(init_argv0) + 1, init_argv0);
		vector[1] = (long)stck_obj + off + 0x1000;
		off += strlen(init_argv0) + 1;

		obj_write_data(bstck, off, strlen(name_id) + 1, name_id);
		vector[3] = (long)stck_obj + off + 0x1000;
		off += strlen(name_id) + 1;

		obj_write_data(bstck, off + tmp, sizeof(long), &vector[0]);
		tmp += sizeof(long);
		obj_write_data(bstck, off + tmp, sizeof(long), &vector[1]);
		tmp += sizeof(long);
		obj_write_data(bstck, off + tmp, sizeof(long), &vector[2]);
		tmp += sizeof(long);
		obj_write_data(bstck, off + tmp, sizeof(long), &vector[3]);
		tmp += sizeof(long);
		obj_write_data(bstck, off + tmp, sizeof(long), &vector[4]);
		tmp += sizeof(long);
		obj_write_data(bstck, off + tmp, sizeof(long), &vector[5]);
		tmp += sizeof(long);

		// obj_write_data(bthr, off + tmp, sizeof(char *) * 4, argv);

		/*
		obj_write_data(bv,
		  __VE_OFFSET
		    + ((uintptr_t)thrd_obj / mm_page_size(MAX_PGLEVEL)) * sizeof(struct viewentry),
		  sizeof(struct viewentry),
		  &v_t);
		  */

		struct sys_thrd_spawn_args tsa = {
			.start_func = (void *)elf.e_entry,
			.stack_base = (void *)stck_obj + 0x1000,
			.stack_size = (US_STACK_SIZE - 0x100),
			.tls_base = stck_obj + 0x1000 + US_STACK_SIZE,
			.arg = stck_obj + off + 0x1000,
			.target_view = bsvid,
			.thrd_ctrl = (uintptr_t)thrd_obj / mm_page_size(MAX_PGLEVEL),
		};
#if 0
		printk("stackbase: %lx, stacktop: %lx\ntlsbase: %lx, arg: %lx\n",
				(long)tsa.stack_base, (long)tsa.stack_base + tsa.stack_size,
				(long)tsa.tls_base, (long)tsa.arg);
#endif
		printk("[kernel] spawning init thread\n");
		r = syscall_thread_spawn(ID_LO(bthrid), ID_HI(bthrid), &tsa, 0, NULL);
		if(r < 0) {
			panic("thread_spawn: %d\n", r);
		}

		obj_put(bthr);
		obj_put(bstck);
		obj_put(bv);
	}
	if((proc->id == 0) && 0) {
		arch_interrupt_set(true);
		while(1) {
			long long a = rdtsc();
			for(volatile long i = 0; i < 1000000000l; i++) {
				asm volatile("" ::: "memory");
			}
			long long b = rdtsc();
			printk("K %d: %lld\n", proc->id, b - a);
		}
	}
#if CONFIG_INSTRUMENT
	kernel_instrument_start();
#endif
#if 0
	printk("processor %d (%s) reached resume state %p\n",
	  proc->id,
	  proc->flags & PROCESSOR_BSP ? "bsp" : "aux",
	  proc);
#endif
	thread_schedule_resume_proc(proc);
}

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <twz/name.h>
#include <twz/sys.h>
#include <twz/thread.h>
#include <twz/view.h>

#include "syscalls.h"

struct process {
	struct thread thrd;
	int pid;
};

#define MAX_PID 1024
static struct process pds[MAX_PID];

#include <elf.h>

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

extern int *__errno_location();
#include <twz/debug.h>
__attribute__((used)) static int __do_exec(uint64_t entry,
  uint64_t _flags,
  uint64_t vidlo,
  uint64_t vidhi,
  void *vector)
{
	(void)_flags;
	objid_t vid = MKID(vidhi, vidlo);

	struct sys_become_args ba = {
		.target_view = vid,
		.target_rip = entry,
		.rdi = (long)vector,
		.rsp = (long)SLOT_TO_VADDR(TWZSLOT_STACK) + 0x200000,
	};
	int r = sys_become(&ba, 0, 0);
	twz_thread_exit(r);
	return 0;
}

extern char **environ;
static int __internal_do_exec(twzobj *view,
  void *entry,
  char const *const *argv,
  char *const *env,
  void *auxbase,
  void *phdr,
  size_t phnum,
  size_t phentsz,
  void *auxentry,
  const char *exename)
{
	if(env == NULL)
		env = environ;

	twzobj stack;
	objid_t sid;
	int r;
	if((r = twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE, 0, 0, &sid))) {
		return r;
	}
	if((r = twz_object_init_guid(&stack, sid, FE_READ | FE_WRITE))) {
		return r;
	}
	if((r = twz_object_tie(view, &stack, 0))) {
		return r;
	}
	if((r = twz_object_delete_guid(sid, 0))) {
		return r;
	}

	uint64_t sp;

	/* calculate space */
	size_t str_space = 0;
	size_t argc = 0;
	size_t envc = 0;
	for(const char *const *s = &argv[0]; *s; s++) {
		str_space += strlen(*s) + 1;
		argc++;
	}

	for(char *const *s = &env[0]; *s; s++) {
		str_space += strlen(*s) + 1;
		envc++;
	}
	str_space += strlen(exename) + 1 + 1 + strlen("TWZEXENAME");

	sp = OBJ_TOPDATA;
	str_space = ((str_space - 1) & ~15) + 16;

	/* TODO: check if we have enough space... */

	/* 5 for: 1 for exename, 1 NULL each for argv and env, argc, and final null after env */
	long *vector_off = (void *)(sp
	                            - (str_space + (argc + envc + 5) * sizeof(char *)
	                               + sizeof(long) * 2 * 32 /* TODO: number of aux vectors */));
	long *vector = twz_object_lea(&stack, vector_off);

	size_t v = 0;
	vector[v++] = argc;
	char *str = (char *)twz_object_lea(&stack, (char *)sp);
	for(size_t i = 0; i < argc; i++) {
		const char *s = argv[i];
		str -= strlen(s) + 1;
		strcpy(str, s);
		vector[v++] = (long)twz_ptr_rebase(TWZSLOT_STACK, str);
	}
	vector[v++] = 0;

	for(size_t i = 0; i < envc; i++) {
		const char *s = env[i];
		str -= strlen(s) + 1;
		strcpy(str, s);
		vector[v++] = (long)twz_ptr_rebase(TWZSLOT_STACK, str);
	}
	str -= strlen(exename) + 1 + 1 + strlen("TWZEXENAME");
	strcpy(str, "TWZEXENAME=");
	strcpy(str + strlen("TWZEXENAME="), exename);
	vector[v++] = (long)twz_ptr_rebase(TWZSLOT_STACK, str);

	vector[v++] = 0;

	vector[v++] = AT_BASE;
	vector[v++] = (long)auxbase;

	vector[v++] = AT_PAGESZ;
	vector[v++] = 0x1000;

	vector[v++] = AT_ENTRY;
	vector[v++] = (long)auxentry;

	vector[v++] = AT_PHNUM;
	vector[v++] = (long)phnum;

	vector[v++] = AT_PHENT;
	vector[v++] = (long)phentsz;

	vector[v++] = AT_PHDR;
	vector[v++] = (long)phdr;

	vector[v++] = AT_UID;
	vector[v++] = 0;
	vector[v++] = AT_GID;
	vector[v++] = 0;
	vector[v++] = AT_EUID;
	vector[v++] = 0;
	vector[v++] = AT_EGID;
	vector[v++] = 0;

	vector[v++] = AT_NULL;
	vector[v++] = 0;

	/* TODO: we should really do this in assembly */
	twz_view_set(view, TWZSLOT_STACK, sid, VE_READ | VE_WRITE);

	// memset(repr->faults, 0, sizeof(repr->faults));
	objid_t vid = twz_object_guid(view);

	/* TODO: copy-in everything for the vector */
	int ret;
	uint64_t p = (uint64_t)SLOT_TO_VADDR(TWZSLOT_STACK) + (OBJ_NULLPAGE_SIZE + 0x200000);
	register long r8 asm("r8") = (long)vector_off + (long)SLOT_TO_VADDR(TWZSLOT_STACK);
	__asm__ __volatile__("movq %%rax, %%rsp\n"
	                     "call __do_exec\n"
	                     : "=a"(ret)
	                     : "a"(p),
	                     "D"((uint64_t)entry),
	                     "S"((uint64_t)(0)),
	                     "d"((uint64_t)vid),
	                     "c"((uint64_t)(vid >> 64)),
	                     "r"(r8));
	twz_thread_exit(ret);
	return ret;
}

static int __internal_load_elf_object(twzobj *view,
  twzobj *elfobj,
  void **base,
  void **phdrs,
  void **entry,
  bool interp)
{
	Elf64_Ehdr *hdr = twz_object_base(elfobj);

	twzobj new_text, new_data;
	int r;
	if((r = twz_object_new(&new_text,
	      NULL,
	      NULL,
	      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC | TWZ_OC_VOLATILE))) {
		return r;
	}
	if((r = twz_object_new(
	      &new_data, NULL, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_VOLATILE))) {
		return r;
	}

	char *phdr_start = (char *)hdr + hdr->e_phoff;
	for(unsigned i = 0; i < hdr->e_phnum; i++) {
		Elf64_Phdr *phdr = (void *)(phdr_start + i * hdr->e_phentsize);
		if(phdr->p_type == PT_LOAD) {
			//		twix_log("load: off=%lx, vaddr=%lx, paddr=%lx, fsz=%lx, msz=%lx\n",
			//		  phdr->p_offset,
			//		  phdr->p_vaddr,
			//		  phdr->p_paddr,
			//		  phdr->p_filesz,
			//		  phdr->p_memsz);
			//	twix_log("  -> %lx %lx\n",
			//	  phdr->p_vaddr & ~(phdr->p_align - 1),
			//	  phdr->p_offset & ~(phdr->p_align - 1));
			twzobj *to;
			if(phdr->p_flags & PF_X) {
				to = &new_text;
			} else {
				to = &new_data;
			}

			char *memstart = twz_object_base(to);
			char *filestart = twz_object_base(elfobj);
			memstart += ((phdr->p_vaddr & ~(phdr->p_align - 1)) % OBJ_MAXSIZE)
			            - (interp ? 0 : OBJ_NULLPAGE_SIZE);
			filestart += phdr->p_offset & ~(phdr->p_align - 1);
			size_t len = phdr->p_filesz;
			len += (phdr->p_offset & (phdr->p_align - 1));
			size_t zerolen = phdr->p_memsz - phdr->p_filesz;
			//		twix_log("  ==> %p %p %lx\n", filestart, memstart, len);
			if((r = sys_ocopy(twz_object_guid(to),
			      twz_object_guid(elfobj),
			      (long)memstart % OBJ_MAXSIZE,
			      (long)filestart % OBJ_MAXSIZE,
			      (len + 0xfff) & ~0xfff,
			      0))) {
				twix_log("oc: %d\n", r);
				return r;
			}
			memset(memstart + phdr->p_filesz, 0, zerolen);

			struct metainfo *mi = twz_object_meta(to);
			mi->flags |= MIF_SZ;
			mi->sz = len + zerolen;

			//		memcpy(memstart, filestart, len);
		}
	}

	if(twz_object_tie(view, &new_text, 0) < 0)
		abort();
	if(twz_object_tie(view, &new_data, 0) < 0)
		abort();
	/* TODO: delete these too */

	size_t base_slot = interp ? 0x10003 : 0;
	twz_view_set(view, base_slot, twz_object_guid(&new_text), VE_READ | VE_EXEC);
	twz_view_set(view, base_slot + 1, twz_object_guid(&new_data), VE_READ | VE_WRITE);

	if(base) {
		*base = (void *)(SLOT_TO_VADDR(base_slot) + OBJ_NULLPAGE_SIZE);
	}
	if(phdrs) {
		/* we don't care about the phdrs for the interpreter, so this only has to be right for the
		 * executable. */
		*phdrs = (void *)(OBJ_NULLPAGE_SIZE + hdr->e_phoff);
	}
	*entry = (base ? (char *)*base : (char *)0) + hdr->e_entry;

	return 0;
}

#if 0
static int __internal_load_elf_exec(twzobj *view, twzobj *elfobj, void **phdr, void **entry)
{
	Elf64_Ehdr *hdr = twz_object_base(elfobj);

	twzobj new_text, new_data;
	int r;
	if((r = twz_object_new(&new_text,
	      NULL,
	      NULL,
	      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC | TWZ_OC_VOLATILE))) {
		return r;
	}
	if((r = twz_object_new(
	      &new_data, NULL, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_VOLATILE))) {
		return r;
	}

	char *phdr_start = (char *)hdr + hdr->e_phoff;
	for(unsigned i = 0; i < hdr->e_phnum; i++) {
		Elf64_Phdr *phdr = (void *)(phdr_start + i * hdr->e_phentsize);
		if(phdr->p_type == PT_LOAD) {
			twix_log("load: off=%lx, vaddr=%lx, paddr=%lx, fsz=%lx, msz=%lx\n",
			  phdr->p_offset,
			  phdr->p_vaddr,
			  phdr->p_paddr,
			  phdr->p_filesz,
			  phdr->p_memsz);
			twix_log("  -> %lx %lx\n",
			  phdr->p_vaddr & ~(phdr->p_align - 1),
			  phdr->p_offset & ~(phdr->p_align - 1));

			twzobj *to;
			if(phdr->p_flags & PF_X) {
				to = &new_text;
			} else {
				to = &new_data;
			}

			char *memstart = twz_object_base(to);
			char *filestart = twz_object_base(elfobj);
			memstart += ((phdr->p_vaddr & ~(phdr->p_align - 1)) % OBJ_MAXSIZE) - OBJ_NULLPAGE_SIZE;
			filestart += phdr->p_offset & ~(phdr->p_align - 1);
			size_t len = phdr->p_filesz;
			len += (phdr->p_offset & (phdr->p_align - 1));
			size_t zerolen = phdr->p_memsz - phdr->p_filesz;
			//	twix_log("  ==> %p %p %lx\n", filestart, memstart, len);
			if((r = sys_ocopy(twz_object_guid(to),
			      twz_object_guid(elfobj),
			      (long)memstart % OBJ_MAXSIZE,
			      (long)filestart % OBJ_MAXSIZE,
			      (len + 0xfff) & ~0xfff,
			      0))) {
				twix_log("oc: %d\n", r);
				return r;
			}
			twix_log("ZEROing %p for len = %lx\n", memstart + phdr->p_filesz, zerolen);
			memset(memstart + phdr->p_filesz, 0, zerolen);
			//			memcpy(memstart, filestart, len);
		}
	}

	/* TODO: actually do tying */
	twz_object_tie(view, &new_text, 0);
	twz_object_tie(view, &new_data, 0);

	twz_view_set(view, 0, twz_object_guid(&new_text), VE_READ | VE_EXEC);
	twz_view_set(view, 1, twz_object_guid(&new_data), VE_READ | VE_WRITE);

	*phdr = (void *)(OBJ_NULLPAGE_SIZE + hdr->e_phoff);
	*entry = (char *)hdr->e_entry;

	return 0;
}
#endif

static long __internal_execve_view_interp(twzobj *view,
  twzobj *exe,
  const char *interp_path,
  const char *const *argv,
  char *const *env,
  const char *exename)
{
	twzobj interp;
	Elf64_Ehdr *hdr = twz_object_base(exe);
	int r;
	if((r = twz_object_init_name(&interp, interp_path, FE_READ))) {
		return r;
	}

	void *interp_base, *interp_entry;
	if((r = __internal_load_elf_object(view, &interp, &interp_base, NULL, &interp_entry, true))) {
		return r;
	}

	void *exe_entry, *phdr;
	if((r = __internal_load_elf_object(view, exe, NULL, &phdr, &exe_entry, false))) {
		return r;
	}

	// twix_log("GOT interp base=%p, entry=%p\n", interp_base, interp_entry);
	// twix_log("GOT phdr=%p, entry=%p\n", phdr, exe_entry);

	__internal_do_exec(view,
	  interp_entry,
	  argv,
	  env,
	  interp_base,
	  phdr,
	  hdr->e_phnum,
	  hdr->e_phentsize,
	  exe_entry,
	  exename);
	return -1;
}
static int __twz_exec_create_view(twzobj *view, objid_t id, objid_t *vid)
{
	int r;
	if((r = twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_USE, 0, 0, vid))) {
		return r;
	}
	if((r = twz_object_init_guid(view, *vid, FE_READ | FE_WRITE))) {
		return r;
	}

	twz_view_set(view, TWZSLOT_CVIEW, *vid, VE_READ | VE_WRITE);

	twz_view_set(view, 0, id, VE_READ | VE_EXEC);

	if((r = twz_object_wire(NULL, view)))
		return r;
	if((r = twz_object_delete(view, 0)))
		return r;

	struct twzview_repr *vr = twz_object_base(view);
	vr->exec_id = id;
	return 0;
}

long linux_sys_execve(const char *path, const char *const *argv, char *const *env)
{
	objid_t id = 0;
	int r = twz_name_resolve(NULL, path, NULL, 0, &id);
	if(r) {
		return r;
	}

	objid_t vid;
	twzobj view;
	if((r = __twz_exec_create_view(&view, id, &vid)) < 0) {
		return r;
	}

	twix_copy_fds(&view);

	twzobj exe;
	twz_object_init_guid(&exe, id, FE_READ);

	char *shbang = twz_object_base(&exe);
	if(*shbang == '#' && *(shbang + 1) == '!') {
		char _cmd[256] = { 0 };
		strncpy(_cmd, shbang + 2, 255);
		char *cmd = _cmd;
		while(*cmd == ' ')
			cmd++;
		char *nl = strchr(cmd, '\n');
		if(nl)
			*nl = 0;
		char *eoc = strchr(cmd, ' ');
		if(eoc) {
			*eoc++ = 0;
			if(*eoc == '-' && (*(eoc + 1) == '-' || *(eoc + 1) == 0))
				eoc = NULL;
		}

		int argc = 0;
		while(argv[argc++])
			;
		const char **new_argv = calloc(argc + 3, sizeof(char *));
		new_argv[0] = argv[0];
		if(eoc) {
			new_argv[1] = eoc;
			new_argv[2] = path;
		} else {
			new_argv[1] = path;
		}
		for(int i = 1; i <= argc; i++) {
			new_argv[i + (eoc ? 2 : 1)] = argv[i];
		}
		return linux_sys_execve(cmd, new_argv, env);
	}

	struct elf64_header *hdr = twz_object_base(&exe);

	kso_set_name(NULL, "[instance] [unix] %s", path);

	char *phdr_start = (char *)hdr + hdr->e_phoff;
	for(unsigned i = 0; i < hdr->e_phnum; i++) {
		Elf64_Phdr *phdr = (void *)(phdr_start + i * hdr->e_phentsize);
		if(phdr->p_type == PT_INTERP) {
			char *interp = (char *)hdr + phdr->p_offset;
			return __internal_execve_view_interp(&view, &exe, interp, argv, env, path);
		}
	}

	return -ENOTSUP;
	// r = twz_exec_view(&view, vid, hdr->e_entry, argv, env);

	// return r;
}

asm(".global __return_from_clone\n"
    "__return_from_clone:"
    "popq %r15;"
    "popq %r14;"
    "popq %r13;"
    "popq %r12;"
    "popq %r11;"
    "popq %r10;"
    "popq %r9;"
    "popq %r8;"
    "popq %rbp;"
    "popq %rsi;"
    "popq %rdi;"
    "popq %rdx;"
    "popq %rbx;"
    "popq %rax;" /* ignore the old rsp */
    "movq $0, %rax;"
    "ret;");

asm(".global __return_from_fork\n"
    "__return_from_fork:"
    "popq %r15;"
    "popq %r14;"
    "popq %r13;"
    "popq %r12;"
    "popq %r11;"
    "popq %r10;"
    "popq %r9;"
    "popq %r8;"
    "popq %rbp;"
    "popq %rsi;"
    "popq %rdi;"
    "popq %rdx;"
    "popq %rbx;"
    "popq %rsp;"
    "movq $0, %rax;"
    "ret;");

extern uint64_t __return_from_clone(void);
extern uint64_t __return_from_fork(void);
long linux_sys_clone(struct twix_register_frame *frame,
  unsigned long flags,
  void *child_stack,
  int *ptid,
  int *ctid,
  unsigned long newtls)
{
	(void)ptid;
	(void)ctid;
	if(flags != 0x7d0f00) {
		return -ENOSYS;
	}

	memcpy((void *)((uintptr_t)child_stack - sizeof(struct twix_register_frame)),
	  frame,
	  sizeof(struct twix_register_frame));
	child_stack = (void *)((uintptr_t)child_stack - sizeof(struct twix_register_frame));
	/* TODO: track these, and when these exit or whatever, release these as well */
	struct thread thr;
	int r;
	if((r = twz_thread_spawn(&thr,
	      &(struct thrd_spawn_args){ .start_func = (void *)__return_from_clone,
	        .arg = NULL,
	        .stack_base = child_stack,
	        .stack_size = 8,
	        .tls_base = (char *)newtls }))) {
		return r;
	}

	/* TODO */
	static _Atomic int __static_thrid = 0;
	return ++__static_thrid;
}

#include <sys/mman.h>

struct mmap_slot {
	twzobj obj;
	int prot;
	int flags;
	size_t slot;
	struct mmap_slot *next;
};

#include <twz/mutex.h>
// static struct mutex mmap_mutex;
static uint8_t mmap_bitmap[TWZSLOT_MMAP_NUM / 8];

static ssize_t __twix_mmap_get_slot(void)
{
	for(size_t i = 0; i < TWZSLOT_MMAP_NUM; i++) {
		if(!(mmap_bitmap[i / 8] & (1 << (i % 8)))) {
			mmap_bitmap[i / 8] |= (1 << (i % 8));
			return i + TWZSLOT_MMAP_BASE;
		}
	}
	return -1;
}

static ssize_t __twix_mmap_take_slot(size_t slot)
{
	// debug_printf(":::: TAKE SLOT %lx %lx\n", slot, slot - TWZSLOT_MMAP_BASE);
	slot -= TWZSLOT_MMAP_BASE;
	if(mmap_bitmap[slot / 8] & (1 << (slot % 8))) {
		return -1;
	}
	mmap_bitmap[slot / 8] |= (1 << (slot % 8));
	return slot + TWZSLOT_MMAP_BASE;
}

static long __internal_mmap_object(void *addr, size_t len, int prot, int flags, int fd, size_t off)
{
	struct file *file = NULL;
	if(fd != -1) {
		file = twix_get_fd(fd);
		if(!file)
			return -EBADF;
	}
	int r;
	twzobj newobj;
	twzobj *obj;
	/* TODO: verify perms */
	uint64_t fe = 0;
	if(prot & PROT_READ)
		fe |= FE_READ;
	if(prot & PROT_WRITE)
		fe |= FE_WRITE;
	if(prot & PROT_EXEC)
		fe |= FE_EXEC;

	uint64_t ve = 0;
	if(prot & PROT_READ)
		ve |= VE_READ;
	if(prot & PROT_WRITE)
		ve |= VE_WRITE;
	if(prot & PROT_EXEC)
		ve |= VE_EXEC;

	twzobj __fobj;
	if(addr) {
		__fobj = twz_object_from_ptr(addr);
	}

#if 0
	twix_log(":::: %p\n", addr);
	if(addr == (void *)0x20000000000 || addr == (void *)0x640000010000 || addr == (void *)0x740000010000
	   || addr == (void *)0x10000000f000 || addr == (void *)0x140000012000) {
		fprintf(stderr, "addr: %p\n", addr);
		fprintf(stderr, "ASAN: %lx\n", len);
		size_t start_slot = VADDR_TO_SLOT(addr);
		size_t end_slot = start_slot + VADDR_TO_SLOT((void *)len) + 1;
			twix_log("%ld -> %ld :: %lx -> %lx\n", start_slot, end_slot, start_slot, end_slot);
		for(size_t i = start_slot; i< end_slot;i++) {
		objid_t nid = 0;
		if(twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_TIED_VIEW, 0, 0, &nid)) {
			return -ENOMEM;
		}
		twz_view_set(NULL, i, nid, FE_READ | FE_WRITE);
		}
		return addr;
	}
#endif

	if(len > OBJ_TOPDATA) {
		len = OBJ_TOPDATA;
	}
	twzobj *fobj = file ? &file->obj : &__fobj;
	struct metainfo *mi = twz_object_meta(fobj);
	if(mi->flags & MIF_SZ) {
		if(len > mi->sz) {
			len = mi->sz;
		}
	}
	size_t adj = 0;
	if(flags & MAP_PRIVATE) {
		// debug_printf(":::: %p -> %lx %lx\n", addr, VADDR_TO_SLOT(addr), TWZSLOT_MMAP_BASE);

		ssize_t slot =
		  fd == -1 ? (ssize_t)VADDR_TO_SLOT(addr)
		           : (addr ? __twix_mmap_take_slot(VADDR_TO_SLOT(addr)) : __twix_mmap_get_slot());
		if(slot < 0) {
			return -ENOMEM;
		}

		if(addr) {
			if((long)addr % OBJ_MAXSIZE < OBJ_NULLPAGE_SIZE) {
				return -EINVAL;
			}
			adj = ((long)addr % OBJ_MAXSIZE);

			if(fd == -1) {
				/* overwriting part of an object */
				if(flags & MAP_ANON) {
					/* TODO: check if object mapped here */
					if((r = sys_ocopy(
					      twz_object_guid(fobj), 0, adj, 0, (len + 0xfff) & ~0xfff, 0))) {
						return r;
					}
					return (long)SLOT_TO_VADDR(slot) + adj;
				} else {
					return -ENOTSUP;
				}
			}
		}

		if((r = twz_object_new(&newobj,
		      NULL,
		      NULL,
		      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC /* TODO */ | TWZ_OC_VOLATILE
		        | TWZ_OC_TIED_VIEW))) {
			return r;
		}

		//	twix_log("mmap create object " IDFMT " --> %lx\n", IDPR(twz_object_guid(&newobj)),
		// slot);

		if((r = sys_ocopy(twz_object_guid(&newobj),
		      twz_object_guid(fobj),
		      OBJ_NULLPAGE_SIZE,
		      off + OBJ_NULLPAGE_SIZE,
		      (len + 0xfff) & ~0xfff,
		      0))) {
			twix_log("ocopy failed: %d\n", r);
			return r;
		}

		struct metainfo *mi = twz_object_meta(&newobj);
		mi->flags |= MIF_SZ;
		mi->sz = len;

		obj = &newobj;
		twz_view_set(NULL, slot, twz_object_guid(&newobj), ve);

		twz_object_release(&newobj);

		return (long)SLOT_TO_VADDR(slot) + OBJ_NULLPAGE_SIZE;
		/*	if(off) {
		        char *src = twz_object_base(&file->obj);
		        char *dst = twz_object_base(obj);

		        memcpy(dst, src + off, len);
		    }*/

	} else {
		obj = fobj;
		adj = off;
	}
	return (long)twz_object_base(obj) + adj;
}
static __inline__ unsigned long long rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

long linux_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off)
{
	// twix_log("sys_mmap: %p %lx %x %x %d %lx\n", addr, len, prot, flags, fd, off);
	if(fd >= 0 || (fd == -1 && addr)) {
		//	size_t s = rdtsc();
		long ret = __internal_mmap_object(addr, len, prot, flags, fd, off);
		//	size_t e = rdtsc();
		//	debug_printf("mmap time %ld\n", e - s);
		//	twix_log("      ==>> %lx\n", ret);
		return ret;
	}
	if(addr != NULL && (flags & MAP_FIXED)) {
		return -ENOTSUP;
	}
	if(!(flags & MAP_ANON)) {
		return -ENOTSUP;
	}

	/* TODO: fix all this up so its better */
	size_t slot = 0x10006ul;
	objid_t o;
	uint32_t fl;
	twz_view_get(NULL, slot, &o, &fl);
	if(!(fl & VE_VALID)) {
		objid_t nid = 0;
		if(twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_TIED_VIEW, 0, 0, &nid)) {
			return -ENOMEM;
		}
		twz_view_set(NULL, slot, nid, FE_READ | FE_WRITE);
	}

	void *base = (void *)(slot * 1024 * 1024 * 1024 + 0x1000);
	struct metainfo *mi = (void *)((slot + 1) * 1024 * 1024 * 1024 - 0x1000);
	uint32_t *next = (uint32_t *)((char *)mi + mi->milen);
	if(*next + len > (1024 * 1024 * 1024 - 0x2000)) {
		return -1; // TODO allocate a new object
	}

	long ret = (long)(base + *next);
	*next += len;
	// twix_log("      ==>> %lx\n", ret);
	return ret;
}

#include <twz/thread.h>
long linux_sys_exit(int code)
{
	twz_thread_exit(code);
	return 0;
}

long linux_sys_set_tid_address()
{
	/* TODO: NI */
	return 0;
}

#include <twz/debug.h>

static bool __fork_view_clone(twzobj *nobj,
  size_t i,
  objid_t oid,
  uint32_t oflags,
  objid_t *nid,
  uint32_t *nflags)
{
	(void)nobj;
	if(i == 0 || (i >= TWZSLOT_ALLOC_START && i <= TWZSLOT_ALLOC_MAX)) {
		*nid = oid;
		*nflags = oflags;
		return true;
	}

	return false;
}

long linux_sys_fork(struct twix_register_frame *frame)
{
	int r;
	twzobj view, cur_view;
	twz_view_object_init(&cur_view);

	// debug_printf("== creating view\n");
	if((r = twz_object_new(
	      &view, &cur_view, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_USE))) {
		return r;
	}

	objid_t vid = twz_object_guid(&view);

	/*if((r = twz_object_wire(NULL, &view)))
	    return r;
	if((r = twz_object_delete(&view, 0)))
	    return r;*/

	int pid = 0;
	for(int i = 1; i < MAX_PID; i++) {
		if(pds[i].pid == 0) {
			pid = i;
			break;
		}
	}

	if(pid == 0) {
		return -1;
	}
	pds[pid].pid = pid;
	// debug_printf("== creating thread\n");
	if(twz_thread_create(&pds[pid].thrd) < 0)
		abort();

	if(twz_view_clone(NULL, &view, 0, __fork_view_clone) < 0)
		abort();

	objid_t sid;
	twzobj stack;
	twz_view_fixedset(
	  &pds[pid].thrd.obj, TWZSLOT_THRD, pds[pid].thrd.tid, VE_READ | VE_WRITE | VE_FIXED);
	/* TODO: handle these */
	if(twz_object_wire_guid(&view, pds[pid].thrd.tid) < 0)
		abort();

	twz_view_set(&view, TWZSLOT_CVIEW, vid, VE_READ | VE_WRITE);

	//	debug_printf("== creating stack\n");
	if((r = twz_object_new(&stack, twz_stdstack, NULL, TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE))) {
		twix_log(":: fork create stack returned %d\n", r);
		abort();
	}
	if(twz_object_tie(&pds[pid].thrd.obj, &stack, 0) < 0)
		abort();
	sid = twz_object_guid(&stack);
	twz_view_set(&view, TWZSLOT_STACK, sid, VE_READ | VE_WRITE);
	// twz_object_wire_guid(&view, sid);

	// twix_log("FORK view = " IDFMT ", stack = " IDFMT "\n",
	//  IDPR(twz_object_guid(&view)),
	//  IDPR(twz_object_guid(&stack)));
	size_t slots_to_copy[] = {
		1, TWZSLOT_UNIX, 0x10004, 0x10006 /* mmap */
	};

	size_t slots_to_tie[] = { 0, 0x10003 };

	/* TODO: move this all to just mmap */
	for(size_t j = 0; j < sizeof(slots_to_tie) / sizeof(slots_to_tie[0]); j++) {
		size_t i = slots_to_tie[j];
		objid_t id;
		uint32_t flags;
		twz_view_get(NULL, i, &id, &flags);
		if(!(flags & VE_VALID)) {
			continue;
		}
		if(twz_object_wire_guid(&view, id) < 0)
			abort();
	}

	for(size_t j = 0; j < sizeof(slots_to_copy) / sizeof(slots_to_copy[0]); j++) {
		size_t i = slots_to_copy[j];
		objid_t id;
		uint32_t flags;
		twz_view_get(NULL, i, &id, &flags);
		if(!(flags & VE_VALID)) {
			continue;
		}
		// twix_log("FORK COPY-DERIVE %lx\n", i);
		/* Copy-derive */
		objid_t nid;
		if((r = twz_object_create(
		      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC /* TODO */ | TWZ_OC_TIED_NONE,
		      0,
		      id,
		      &nid))) {
			/* TODO: cleanup */
			return r;
		}
		//	twix_log("FORK COPY-DERIVE %lx: " IDFMT " --> " IDFMT "\n", i, IDPR(id), IDPR(nid));
		if(flags & VE_FIXED) {
		}
		//		twz_view_fixedset(&pds[pid].thrd.obj, i, nid, flags);
		else
			twz_view_set(&view, i, nid, flags);
		if(twz_object_wire_guid(&view, nid) < 0)
			abort();
		if(twz_object_delete_guid(nid, 0) < 0)
			abort();
	}

	for(size_t j = TWZSLOT_MMAP_BASE; j < TWZSLOT_MMAP_BASE + TWZSLOT_MMAP_NUM; j++) {
		size_t i = j;
		objid_t id;
		uint32_t flags;
		twz_view_get(NULL, i, &id, &flags);
		if(!(flags & VE_VALID) || !(flags & VE_WRITE)) {
			if(flags & VE_VALID) {
				if(twz_object_wire_guid(&view, id) < 0)
					abort();
			}
			continue;
		}
		/* Copy-derive */
		objid_t nid;
		if((r = twz_object_create(
		      TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE | TWZ_OC_DFL_EXEC /* TODO */ | TWZ_OC_TIED_NONE,
		      0,
		      id,
		      &nid))) {
			/* TODO: cleanup */
			return r;
		}
		//	twix_log("FORK COPY-DERIVE %lx: " IDFMT " --> " IDFMT "\n", i, IDPR(id), IDPR(nid));
		if(flags & VE_FIXED) {
		}
		//		twz_view_fixedset(&pds[pid].thrd.obj, i, nid, flags);
		else
			twz_view_set(&view, i, nid, flags);
		if(twz_object_wire_guid(&view, nid) < 0)
			abort();
		if(twz_object_delete_guid(nid, 0) < 0)
			abort();
	}

	// twz_object_wire(NULL, &stack);
	// twz_object_delete(&stack, 0);

	// size_t soff = (uint64_t)twz_ptr_local(frame) - 1024;
	// void *childstack = twz_object_lea(&stack, (void *)soff);

	// memcpy(childstack, frame, sizeof(struct twix_register_frame));

	uint64_t fs;
	asm volatile("rdfsbase %%rax" : "=a"(fs));

	struct sys_thrd_spawn_args sa = {
		.target_view = vid,
		.start_func = (void *)__return_from_fork,
		.arg = NULL,
		.stack_base = (void *)frame, // twz_ptr_rebase(TWZSLOT_STACK, soff),
		.stack_size = 8,
		.tls_base = (void *)fs,
		.thrd_ctrl = TWZSLOT_THRD,
	};

	//	debug_printf("== spawning\n");
	if((r = sys_thrd_spawn(pds[pid].thrd.tid, &sa, 0, NULL))) {
		return r;
	}

	if(twz_object_tie(NULL, &view, TIE_UNTIE) < 0)
		abort();
	if(twz_object_tie(NULL, &stack, TIE_UNTIE) < 0)
		abort();
	twz_object_release(&view);
	twz_object_release(&stack);

	return pid;
}

#include <twz/debug.h>
struct rusage;
long linux_sys_wait4(long pid, int *wstatus, int options, struct rusage *rusage)
{
	(void)pid;
	(void)options;
	(void)rusage;
	while(1) {
		struct thread *thrd[MAX_PID];
		int sps[MAX_PID];
		long event[MAX_PID] = { 0 };
		uint64_t info[MAX_PID];
		int pids[MAX_PID];
		size_t c = 0;
		for(int i = 0; i < MAX_PID; i++) {
			if(pds[i].pid) {
				sps[c] = THRD_SYNC_EXIT;
				pids[c] = i;
				thrd[c++] = &pds[i].thrd;
			}
		}
		if(c == 0 || (options & WNOHANG)) {
			/* TODO */
			return -ECHILD;
		}
		if(!(options & WNOHANG)) {
			int r = twz_thread_wait(c, thrd, sps, event, info);
			if(r < 0) {
				return r;
			}
		}

		for(unsigned int i = 0; i < c; i++) {
			if(event[i] && pds[pids[i]].pid) {
				if(wstatus) {
					*wstatus = 0; // TODO
				}
				pds[pids[i]].pid = 0;
				twz_thread_release(&pds[pids[i]].thrd);
				return pids[i];
			}
		}
		if(options & WNOHANG) {
			return 0;
		}
	}
}

#include <twz/debug.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_FD 2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12

#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

long linux_sys_futex(int *uaddr,
  int op,
  int val,
  const struct timespec *timeout,
  int *uaddr2,
  int val3)
{
	(void)timeout;
	(void)uaddr2;
	(void)val3;
	switch((op & FUTEX_CMD_MASK)) {
		case FUTEX_WAIT:
			twz_thread_sync32(
			  THREAD_SYNC_SLEEP, (_Atomic unsigned int *)uaddr, val, (struct timespec *)timeout);
			return 0; // TODO
			break;
		case FUTEX_WAKE:
			twz_thread_sync32(THREAD_SYNC_WAKE, (_Atomic unsigned int *)uaddr, val, NULL);
			return 0; // TODO
			break;
		default:
			twix_log("futex %d: %p (%x) %x\n", op, uaddr, uaddr ? *uaddr : 0, val);
			return -ENOTSUP;
	}
	return 0;
}

long linux_sys_gettid()
{
	return 1;
}

long linux_sys_getpid()
{
	return 1;
}

long linux_sys_getppid()
{
	return 1;
}

long linux_sys_getpgid()
{
	return 1;
}

long linux_sys_prlimit()
{
	return -ENOSYS;
}

long linux_sys_mprotect()
{
	return 0;
	return -ENOSYS;
}

long linux_sys_madvise()
{
	return 0;
}

long linux_sys_munmap()
{
	return 0;
	return -ENOSYS;
}

long linux_sys_getrlimit()
{
	return -ENOSYS;
}

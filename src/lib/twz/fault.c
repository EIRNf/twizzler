#include <stdio.h>
#include <twz/_err.h>
#include <twz/debug.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/objid.h>
#include <twz/ptr.h>
#include <twz/sys/fault.h>
#include <twz/sys/obj.h>
#include <twz/sys/thread.h>
#include <twz/sys/view.h>

#include <twz.h>

#define EXIT_CODE_FAULT(f) ({ 256 + (f); })

#define PRINT(...) fprintf(stderr, ##__VA_ARGS__)
//#define PRINT(...)

struct {
	void (*_Atomic fn)(int, void *, void *);
	void *userdata;
} _fault_table[NUM_FAULTS] = {};

#if 0
#define FPR(s)                                                                                     \
	debug_printf("  -- FAULT: " s " (ip=%p, addr=%p, id=" IDFMT ")\n", source, addr, IDPR(id))
#endif

#define FPR(...)

static int twz_map_fot_entry(twzobj *obj,
  size_t slot,
  struct fotentry *fe,
  objid_t srcid,
  void *addr,
  void *ip)
{
	objid_t id;
	if(fe->flags & FE_NAME) {
		int r = twz_name_resolve(obj, fe->name.data, fe->name.nresolver, 0, &id);
		if(r < 0) {
			struct fault_pptr_info fi = twz_fault_build_pptr_info(
			  srcid, slot, ip, FAULT_PPTR_RESOLVE, r, 0, fe->name.data, (void *)addr);
			twz_fault_raise(FAULT_PPTR, &fi);
			return r;
		}
	} else {
		id = fe->id;
	}

	int flags = ((fe->flags & FE_READ) ? VE_READ : 0) | ((fe->flags & FE_WRITE) ? VE_WRITE : 0)
	            | ((fe->flags & FE_EXEC) ? VE_EXEC : 0);

	if(fe->flags & FE_DERIVE) {
		objid_t nid;
		int err;
		if((err = twz_object_create(TWZ_OC_DFL_READ | TWZ_OC_DFL_WRITE, 0, id, &nid)) < 0) {
			struct fault_pptr_info fi = twz_fault_build_pptr_info(
			  srcid, slot, ip, FAULT_PPTR_DERIVE, err, 0, NULL, (void *)addr);
			twz_fault_raise(FAULT_PPTR, &fi);
			return err;
		}
		id = nid;
	}

	twz_view_set(NULL, slot, id, flags);
	if(fe->flags & FE_DERIVE) {
		if(twz_object_wire_guid(NULL, id)) {
			libtwz_panic("failed to wire guid during fault handling");
		}
		if(twz_object_delete_guid(id, 0)) {
			libtwz_panic("failed to delete object during fault handling");
		}
	}
	return 0;
}

static int twz_handle_fault(void *addr, int cause, void *source, objid_t id)
{
	uintptr_t offset = (uintptr_t)twz_ptr_local(addr);
	if(offset < OBJ_NULLPAGE_SIZE) {
		FPR("NULL pointer");
		return -EINVAL;
	}

	if(!(cause & FAULT_OBJECT_NOMAP)) {
		FPR("Protection Error");
		return -EACCES;
	}

	if(cause & FAULT_OBJECT_EXIST) {
		FPR("Object does not exist");
		return -ENOENT;
	}

	if(cause & FAULT_OBJECT_UNSIZED) {
		FPR("Tried to perform sized operation on unsized object");
	}

	uint32_t obj0flags;
	objid_t obj0id;
	twz_view_get(NULL, 0, &obj0id, &obj0flags);
	if(!(obj0flags & VE_VALID) || obj0id == 0) {
		FPR("Location not mapped");
		return -EINVAL;
	}

	twzobj o0;
	twz_object_init_ptr(NULL, &o0);
	struct metainfo *mi = twz_object_meta(&o0);
	if(mi->magic != MI_MAGIC) {
		FPR("Invalid object");
		return -EINVLOBJ;
	}

	size_t slot = VADDR_TO_SLOT(addr);
	struct fotentry *fe = _twz_object_get_fote(&o0, slot);

	if(!fe) {
		FPR("Invalid pointer");
		return -EINVAL;
	}

	if(!(atomic_load(&fe->flags) & _FE_VALID) || fe->id == 0) {
		struct fault_pptr_info fi =
		  twz_fault_build_pptr_info(id, slot, source, FAULT_PPTR_INVALID, 0, 0, NULL, addr);
		twz_fault_raise(FAULT_PPTR, &fi);
		return -ENOENT;
	}

	return twz_map_fot_entry(&o0, slot, fe, id, addr, source);
}

static int __fault_obj_default(int fault, struct fault_object_info *info)
{
	(void)fault;
	return twz_handle_fault(info->addr, info->flags, info->ip, info->objid);
}

struct fault_frame {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rsp;
};

static const char *fault_names[] = {
	[FAULT_OBJECT] = "FAULT_OBJECT",
	[FAULT_FAULT] = "FAULT_FAULT",
	[FAULT_NULL] = "FAULT_NULL",
	[FAULT_SCTX] = "FAULT_SCTX",
	[FAULT_EXCEPTION] = "FAULT_EXCEPTION",
	[FAULT_PPTR] = "FAULT_PPTR",
	[FAULT_PAGE] = "FAULT_PAGE",
	[FAULT_SIGNAL] = "FAULT_SIGNAL",
};

struct stackframe {
	struct stackframe *ebp;
	uint64_t eip;
};

#include <twz/sys/sctx.h>
static void __twz_fault_unhandled_print(int fault_nr, void *data)
{
	struct twzthread_repr *repr = twz_thread_repr_base();
	PRINT("  from thrd %s\n", repr->hdr.name);
	// if(info->fault_nr != FAULT_NULL)
	//	twz_thread_exit();
	if(fault_nr == FAULT_SCTX) {
		struct fault_sctx_info *si = (void *)data;
		char rp[12];
		int _r = 0;
		if(si->pneed & SCP_READ)
			rp[_r++] = 'r';
		if(si->pneed & SCP_WRITE)
			rp[_r++] = 'w';
		if(si->pneed & SCP_EXEC)
			rp[_r++] = 'x';
		rp[_r] = 0;
		PRINT("  when accessing " IDFMT " at addr %p, requesting %s (%x)\n",
		  IDPR(si->target),
		  si->addr,
		  rp,
		  si->pneed);
	} else if(fault_nr == FAULT_EXCEPTION) {
		struct fault_exception_info *ei = (void *)data;
		PRINT("  ecode: %ld, ip: %p, info: %lx\n", ei->code, ei->ip, ei->arg0);
	} else if(fault_nr == FAULT_OBJECT) {
		struct fault_object_info *fobj = data;
		PRINT("  id = " IDFMT "; flags = %lx\n", IDPR(fobj->objid), fobj->flags);
		PRINT("  ip = %p, addr = %p\n", fobj->ip, fobj->addr);
	} else if(fault_nr == FAULT_PPTR) {
		struct fault_pptr_info *pi = (void *)data;
		PRINT("objid: " IDFMT "; fote: %ld\n", IDPR(pi->objid), pi->fote);
		PRINT(
		  "ip: %p; info: %x; retval: %x; flags: %lx\n", pi->ip, pi->info, pi->retval, pi->flags);
		PRINT("name: %p; ptr: %p\n", pi->name, pi->ptr);
	}

#if 0
	struct stackframe *sf = (void *)frame->rbp;
	while(sf) {
		PRINT("  (%p) : %lx\n", sf, sf->eip);
		sf = sf->ebp;
	}
#endif
}

#include <twz/debug.h>
#include <twz/twztry.h>
_Atomic int _twz_try_okay = 0;
_Thread_local jmp_buf *_Atomic _twz_jmp_buf = NULL;
_Thread_local void *_Atomic _twz_excep_data = NULL;
static void _twz_default_exception_handler(int fault, void *data)
{
	if(!_twz_try_okay)
		return;
	if(_twz_jmp_buf) {
		_twz_excep_data = data;
		longjmp(*_twz_jmp_buf, fault + 1);
	}
}

static void __twz_fault_unhandled(struct fault_fault_info *info, struct fault_frame *frame)
{
	struct twzthread_repr *repr = twz_thread_repr_base();
	PRINT("\e[1;31munhandled fault\e[0;1m: \e[1;35m%s\e[0;1m in thread " IDFMT " (%s)\e[0m\n",
	  fault_names[info->fault_nr],
	  IDPR(repr->reprid),
	  repr->hdr.name);
	uint64_t *pp = (void *)(frame->rbp + 8);
	PRINT("  occurred at: %lx\n", pp[0]);
	__twz_fault_unhandled_print(info->fault_nr, info->data);

	libtwz_do_backtrace();
}

void __twix_signal_handler(int fault, void *data, void *userdata);
__attribute__((used)) void __twz_fault_entry_c(int fault, void *_info, struct fault_frame *frame)
{
	/* we provide default handling for FAULT_OBJECT that always runs. We also handle double-faults
	 * here explicitly. Unlike FAULT_OBJECT, a thread cannot catch double-faults, and they are
	 * fatal. */
	// debug_printf("handling a fault %d %p\n", fault, _fault_table);

	if(fault == FAULT_OBJECT) {
		if(__fault_obj_default(fault, _info) < 0) {
			if(_fault_table[fault].fn) {
				_fault_table[fault].fn(fault, _info, _fault_table[fault].userdata);
				return;
			}
			_twz_default_exception_handler(fault, _info);
			fprintf(stderr, "  -- FAULT %d: unhandled.\n", fault);
			__twz_fault_unhandled_print(fault, _info);
			twz_thread_exit(EXIT_CODE_FAULT(fault));
		}
	} else if(fault == FAULT_FAULT) {
		struct fault_fault_info *ffi = _info;
		_twz_default_exception_handler(ffi->fault_nr, ffi->data);
		__twz_fault_unhandled(_info, frame);
		twz_thread_exit(EXIT_CODE_FAULT(fault));
		return;
	}

	// debug_printf("            handling a fault %d %p\n", fault, _fault_table[fault].fn);
	if((fault >= NUM_FAULTS || !_fault_table[fault].fn) && fault != FAULT_OBJECT) {
		if(fault == 7) {
			__twix_signal_handler(fault, _info, NULL);
			return;
		}
		// debug_printf("                              handling a fault %d\n", fault);
		_twz_default_exception_handler(fault, _info);
		fprintf(stderr, "  -- FAULT %d: unhandled.\n", fault);
		__twz_fault_unhandled_print(fault, _info);
		twz_thread_exit(EXIT_CODE_FAULT(fault));
	}
	if(_fault_table[fault].fn) {
		_fault_table[fault].fn(fault, _info, _fault_table[fault].userdata);
	}
}

/* TODO: arch-dep */
/* Stack comes in as mis-aligned (like any function call),
 * so maintain that alignment until the call below. */

/* the CFI directives are there to indicate to the debugging system where the canonical frame
 * address is (since we no longer rely on rbp). In this case, the kernel pushes both the return IP
 * and RBP (and RSP) before jumping here. It then set the RBP to be the frame pointer as if this
 * were a function call. So set the CFA to 16 off RBP, as normal (this looks like a function without
 * a prologue; the prologue is provided by the kernel) */
asm(" \
		.cfi_sections .debug_frame ;\
        .extern __twz_fault_entry_c ;\
		.globl __twz_fault_entry;\
		.type __twz_fault_entry, @function;\
        __twz_fault_entry: ;\
						.cfi_startproc;\
						.cfi_def_cfa rbp, 144; \
                        pushq %rax;\
                        pushq %rbx;\
                        pushq %rcx;\
                        pushq %rdx;\
                        pushq %rbp;\
                        pushq %r8;\
                        pushq %r9;\
                        pushq %r10;\
                        pushq %r11;\
                        pushq %r12;\
                        pushq %r13;\
                        pushq %r14;\
                        pushq %r15;\
\
						mov %rsp, %rdx;\
                        call __twz_fault_entry_c ;\
\
                        popq %r15;\
                        popq %r14;\
                        popq %r13;\
                        popq %r12;\
                        popq %r11;\
                        popq %r10;\
                        popq %r9;\
                        popq %r8;\
                        popq %rbp;\
                        popq %rdx;\
                        popq %rcx;\
                        popq %rbx;\
                        popq %rax;\
\
                        popq %rdi;\
                        popq %rsi;\
                        popq %rsp;\
\
                        subq $144, %rsp;\
						popq %rbp;\
						addq $136, %rsp;\
						jmp *-136(%rsp);\
		.cfi_endproc;\
        ");
/* the fault handler has to take the red zone into account. But we also can't burn a register. So
 * restore the stack and then jmp to where we stored the IP (128 + 8). Basically, we're restoring
 * the stack and returing to the address the kernel pushed; we know where it is, it's just not
 * "nearby". */

void __twz_fault_entry(void);
__attribute__((used, visibility("default"))) void __twz_fault_init(void)
{
	{
		struct twzview_repr *repr = (struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW);
		repr->fault_mask = 0;
		repr->fault_flags = 0;
		repr->fault_handler = __twz_fault_entry;
		repr->dbl_fault_handler = __twz_fault_entry;
	}
}

void twz_fault_set_upcall_entry(void *p, void *pd)
{
	struct twzview_repr *repr = (struct twzview_repr *)twz_slot_to_base(TWZSLOT_CVIEW);
	repr->fault_mask = 0;
	repr->fault_flags = 0;
	repr->fault_handler = p;
	repr->dbl_fault_handler = pd;
}

void *twz_fault_get_userdata(int fault)
{
	return _fault_table[fault].userdata;
}

int twz_fault_set(int fault, void (*fn)(int, void *, void *), void *userdata)
{
	// debug_printf("setting fault %d -> %p %p\n", fault, _fault_table, fn);
	_fault_table[fault].fn = fn;
	_fault_table[fault].userdata = userdata;
	/*
	struct twzthread_repr *repr = twz_thread_repr_base();

	repr->faults[fault] = (struct faultinfo){
	    .addr = fn ? (void *)__twz_fault_entry : NULL,
	};
	*/
	return 0;
}

void _twz_try_unhandled(int fault, void *data)
{
	fprintf(stderr, "  -- FAULT %d: unhandled (returned from try block).\n", fault);
	__twz_fault_unhandled_print(fault, data);
	libtwz_do_backtrace();
	twz_thread_exit(EXIT_CODE_FAULT(fault));
}

void twz_fault_raise(int fault, void *data)
{
	void (*fn)(int, void *, void *) = atomic_load(&_fault_table[fault].fn);
	void *userdata = _fault_table[fault].userdata;
	if(fn) {
		fn(fault, data, userdata);
	} else {
		_twz_default_exception_handler(fault, data);
		fprintf(stderr, "  -- RAISE FAULT %d: unhandled.\n", fault);
		__twz_fault_unhandled_print(fault, data);
		libtwz_do_backtrace();
		twz_thread_exit(EXIT_CODE_FAULT(fault));
	}
}

void twz_fault_raise_data(int fault, void *data, void *userdata)
{
	void (*fn)(int, void *, void *) = atomic_load(&_fault_table[fault].fn);
	if(fn) {
		fn(fault, data, userdata);
	} else {
		_twz_default_exception_handler(fault, data);
		fprintf(stderr, "  -- RAISE FAULT %d: unhandled.\n", fault);
		__twz_fault_unhandled_print(fault, data);
		libtwz_do_backtrace();
		twz_thread_exit(EXIT_CODE_FAULT(fault));
	}
}

#include <kalloc.h>
#include <processor.h>
#include <syscall.h>
#include <thread.h>

void arch_thread_print_info(struct thread *t)
{
	if(t->arch.was_syscall) {
		printk("  was syscall, rip %lx\n", t->arch.syscall.rcx);
	} else {
		printk("  was int, rip %lx\n", t->arch.exception.rip);
	}
}

long arch_thread_syscall_num(void)
{
	if(!current_thread)
		return -1;
	return current_thread->arch.syscall.rax;
}

uintptr_t arch_thread_instruction_pointer(void)
{
	if(!current_thread)
		panic("cannot call %s before threading", __FUNCTION__);
	if(current_thread->arch.was_syscall) {
		return current_thread->arch.syscall.rcx;
	} else {
		return current_thread->arch.exception.rip;
	}
}

void arch_thread_become_restore(struct thread_become_frame *frame, long *args)
{
	current_thread->arch.syscall.rax = frame->frame.rax;
	current_thread->arch.syscall.rbx = frame->frame.rbx;
	current_thread->arch.syscall.rcx = frame->frame.rcx;
	current_thread->arch.syscall.rdx = frame->frame.rdx;
	current_thread->arch.syscall.rdi = frame->frame.rdi;
	current_thread->arch.syscall.rsi = frame->frame.rsi;
	current_thread->arch.syscall.rbp = frame->frame.rbp;
	current_thread->arch.syscall.rsp = frame->frame.rsp;
	current_thread->arch.syscall.r8 = frame->frame.r8;
	current_thread->arch.syscall.r9 = frame->frame.r9;
	current_thread->arch.syscall.r10 = frame->frame.r10;
	current_thread->arch.syscall.r11 = frame->frame.r11;
	current_thread->arch.syscall.r12 = frame->frame.r12;
	current_thread->arch.syscall.r13 = frame->frame.r13;
	current_thread->arch.syscall.r14 = frame->frame.r14;
	current_thread->arch.syscall.r15 = frame->frame.r15;
	current_thread->arch.fs = frame->frame.fs;
	current_thread->arch.gs = frame->frame.gs;
	if(args) {
		long arg_count = args[0];
		for(long i = 0; i < 6 && i < arg_count; i++) {
			switch(i) {
				case 0:
					current_thread->arch.syscall.rdi = args[i + 1];
					break;
				case 1:
					current_thread->arch.syscall.rsi = args[i + 1];
					break;
				case 2:
					current_thread->arch.syscall.rdx = args[i + 1];
					break;
				case 3:
					current_thread->arch.syscall.r8 = args[i + 1];
					break;
				case 4:
					current_thread->arch.syscall.r9 = args[i + 1];
					break;
				case 5:
					current_thread->arch.syscall.r10 = args[i + 1];
					break;
			}
		}
	}
}

void arch_thread_become(struct arch_syscall_become_args *ba,
  struct thread_become_frame *frame,
  bool noupdate)
{
	if(current_thread->arch.was_syscall) {
		if(frame) {
			frame->frame.rax = current_thread->arch.syscall.rax;
			frame->frame.rbx = current_thread->arch.syscall.rbx;
			frame->frame.rcx = current_thread->arch.syscall.rcx;
			frame->frame.rdx = current_thread->arch.syscall.rdx;
			frame->frame.rdi = current_thread->arch.syscall.rdi;
			frame->frame.rsi = current_thread->arch.syscall.rsi;
			frame->frame.rbp = current_thread->arch.syscall.rbp;
			frame->frame.rsp = current_thread->arch.syscall.rsp;
			frame->frame.r8 = current_thread->arch.syscall.r8;
			frame->frame.r9 = current_thread->arch.syscall.r9;
			frame->frame.r10 = current_thread->arch.syscall.r10;
			frame->frame.r11 = current_thread->arch.syscall.r11;
			frame->frame.r12 = current_thread->arch.syscall.r12;
			frame->frame.r13 = current_thread->arch.syscall.r13;
			frame->frame.r14 = current_thread->arch.syscall.r14;
			frame->frame.r15 = current_thread->arch.syscall.r15;
			frame->frame.fs = current_thread->arch.fs;
			frame->frame.gs = current_thread->arch.gs;
		}

		if(noupdate)
			return;

		current_thread->arch.syscall.rax = ba->rax;
		current_thread->arch.syscall.rbx = ba->rbx;
		/* note: rcx holds return RIP, so don't set it */
		current_thread->arch.syscall.rdx = ba->rdx;
		current_thread->arch.syscall.rdi = ba->rdi;
		current_thread->arch.syscall.rsi = ba->rsi;
		current_thread->arch.syscall.rbp = ba->rbp;
		current_thread->arch.syscall.rsp = ba->rsp;
		current_thread->arch.syscall.r8 = ba->r8;
		current_thread->arch.syscall.r9 = ba->r9;
		current_thread->arch.syscall.r10 = ba->r10;
		current_thread->arch.syscall.r11 = ba->r11;
		current_thread->arch.syscall.r12 = ba->r12;
		current_thread->arch.syscall.r13 = ba->r13;
		current_thread->arch.syscall.r14 = ba->r14;
		current_thread->arch.syscall.r15 = ba->r15;

		current_thread->arch.syscall.rcx = ba->target_rip;
	} else {
		if(frame) {
			panic("NI - frame backup for become due to exception");
		}
		if(noupdate)
			return;
		current_thread->arch.exception.rax = ba->rax;
		current_thread->arch.exception.rbx = ba->rbx;
		current_thread->arch.exception.rcx = ba->rcx;
		current_thread->arch.exception.rdx = ba->rdx;
		current_thread->arch.exception.rdi = ba->rdi;
		current_thread->arch.exception.rsi = ba->rsi;
		current_thread->arch.exception.rbp = ba->rbp;
		current_thread->arch.exception.userrsp = ba->rsp;
		current_thread->arch.exception.r8 = ba->r8;
		current_thread->arch.exception.r9 = ba->r9;
		current_thread->arch.exception.r10 = ba->r10;
		current_thread->arch.exception.r11 = ba->r11;
		current_thread->arch.exception.r12 = ba->r12;
		current_thread->arch.exception.r13 = ba->r13;
		current_thread->arch.exception.r14 = ba->r14;
		current_thread->arch.exception.r15 = ba->r15;

		current_thread->arch.exception.rip = ba->target_rip;
	}
}

int arch_syscall_thrd_ctl(int op, long arg)
{
	switch(op) {
		case THRD_CTL_SET_FS:
			if(!verify_user_pointer((void *)arg, sizeof(void *))) {
				return -EINVAL;
			}
			current_thread->arch.fs = arg;
			break;
		case THRD_CTL_SET_GS:
			if(!verify_user_pointer((void *)arg, sizeof(void *))) {
				return -EINVAL;
			}
			current_thread->arch.gs = arg;
			break;
		case THRD_CTL_SET_IOPL:
			/* TODO (sec): permission check */
			current_thread->arch.syscall.r11 |= ((arg & 3) << 12);
			break;
		default:
			return -1;
	}
	return 0;
}

void arch_thread_raise_call(struct thread *t, void *addr, long a0, void *info, size_t infolen)
{
	if(t != current_thread) {
		// panic("NI - raise fault in non-current thread");
	}

	uint64_t *arg0, *arg1, *jmp, *stack, *rsp, *rbp;

	if(t->arch.was_syscall) {
		stack = (uint64_t *)t->arch.syscall.rsp;
		arg0 = &t->arch.syscall.rdi;
		arg1 = &t->arch.syscall.rsi;
		jmp = &t->arch.syscall.rcx;
		rsp = &t->arch.syscall.rsp;
		rbp = &t->arch.syscall.rbp;
	} else {
		stack = (uint64_t *)t->arch.exception.userrsp;
		arg0 = &t->arch.exception.rdi;
		arg1 = &t->arch.exception.rsi;
		jmp = &t->arch.exception.rip;
		rsp = &t->arch.exception.userrsp;
		rbp = &t->arch.exception.rbp;
	}

	long red_zone = 128;

	uintptr_t stack_after = (uintptr_t)stack - (infolen + 5 * 8 + red_zone);
	uintptr_t bot_stack = ((uintptr_t)stack & ~(OBJ_MAXSIZE - 1)) + OBJ_NULLPAGE_SIZE;
	if(stack_after <= bot_stack) {
		printk("thread %ld exceeded stack during fault raise\n", t->id);
		thread_exit();
		return;
	}

	if(!VADDR_IS_USER((uintptr_t)stack)) {
		struct object *to = kso_get_obj(t->throbj, thr);
		struct kso_hdr *kh = obj_get_kbase(to);
		printk("thread %ld (%s) has corrupted stack pointer (%p)\n", t->id, kh->name, stack);
		obj_put(to);
		thread_exit();
		return;
	}

	if(!VADDR_IS_USER((uintptr_t)addr)) {
		printk("thread %ld tried to jump into kernel (addr %p)\n", t->id, addr);
		thread_exit();
		return;
	}

	if((uintptr_t)stack < OBJ_NULLPAGE_SIZE) {
		printk("thread %ld has invalid stack\n", t->id);
		thread_exit();
		return;
	}

	/* TODO: validate that stack is in a reasonable object */
	if(((unsigned long)stack & 0xFFFFFFFFFFFFFFF0) != (unsigned long)stack) {
		//	panic("NI");
		/* set up stack alignment correctly
		 * (mis-aligned going into a function call) */
		stack--;
	}

	stack -= red_zone / 8;

	*--stack = *jmp;
	*--stack = *rbp;
	// printk("raise: from %lx, orig frame = %lx\n", *jmp, *rbp);
	// printk("raise: Setting stack = %p = %lx\n", stack, *rbp);
	*rbp = (long)stack;
	// printk("raise: Setting rbp = %lx\n", *rbp);

	if(infolen & 0xf) {
		panic("infolen must be 16-byte aligned (was %ld)", infolen);
	}

	stack -= infolen / 8;
	long info_base_user = (long)stack;
	memcpy(stack, info, infolen);

	*--stack = *rsp;
	*--stack = *arg1;
	*--stack = *arg0;
	*jmp = (long)addr;
	*arg0 = a0;
	*arg1 = info_base_user;
	*rsp = (long)stack;
}

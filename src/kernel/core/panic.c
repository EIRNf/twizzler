#include <debug.h>
#include <interrupt.h>
#include <page.h>
#include <processor.h>
#include <stdarg.h>

static struct spinlock panic_lock = SPINLOCK_INIT;

void __panic(const char *file, int linenr, int flags, const char *msg, ...)
{
	arch_interrupt_set(false);
	spinlock_acquire(&panic_lock);
	/* TODO (minor): stop processors, cli */
	// processor_send_ipi(PROCESSOR_IPI_DEST_OTHERS, PROCESSOR_IPI_HALT, NULL, 0);
	va_list args;
	va_start(args, msg);
	printk("panic [%s:%d] - ",
	  // current_processor ? current_processor->id : 0,
	  // current_thread ? (long)current_thread->id : -1,
	  file,
	  linenr);
	vprintk(msg, args);
	printk("\n");

	if(flags & PANIC_UNWIND)
		debug_print_backtrace();
	mm_page_print_stats();
	if(current_thread) {
		printk("in-kernel from: %s\n", current_thread->arch.was_syscall ? "syscall" : "exception");
		printk("  NR: %ld\n",
		  current_thread->arch.was_syscall ? current_thread->arch.syscall.rax
		                                   : current_thread->arch.exception.int_no);
		uintptr_t pc = current_thread->arch.was_syscall ? current_thread->arch.syscall.rcx
		                                                : current_thread->arch.exception.rip;
		printk("  pc: %lx\n", pc);
		/*
		unsigned char *ip = (void *)pc;
		for(int i = 0; i < 32; i++)
		    printk("%x ", *(ip + i));
		*/
		printk("  thread: %ld\n", current_thread->id);
		printk("\n");
	}
	spinlock_release(&panic_lock, 0);
	// kernel_debug_entry();
	if(!(flags & PANIC_CONTINUE))
		for(;;)
			;
}

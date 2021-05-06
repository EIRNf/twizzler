#include <debug.h>
#include <ksymbol.h>
#include <system.h>
#if FEATURE_SUPPORTED_UNWIND
#ifdef __clang__
__attribute__((no_sanitize("alignment")))
#else
__attribute__((no_sanitize_undefined))
#endif

static void
__print_frame(struct frame *frame)
{
	const char *name = debug_symbolize((void *)frame->pc);
	if(name) {
		printk("  %lx < %s >\n", frame->pc, name);
	} else {
		printk("  %lx < ?? >\n", frame->pc);
	}
}
#endif
#ifdef __clang__
__attribute__((no_sanitize("alignment")))
#else
__attribute__((no_sanitize_undefined))
#endif

void debug_print_backtrace(void)
{
#if FEATURE_SUPPORTED_UNWIND
	struct frame frame;
	frame.pc = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
	frame.fp = (uintptr_t)__builtin_frame_address(0);
	printk("STACK TRACE (from %lx):\n", frame.fp);
	while(arch_debug_unwind_frame(&frame)) {
		__print_frame(&frame);
	}

#else
	printk("Arch '%s' does not support unwinding.\n", stringify_define(CONFIG_ARCH));
#endif
}

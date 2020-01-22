#include <arch/x86_64-msr.h>
#include <processor.h>

void arch_processor_enumerate()
{
	/* this is handled by initializers in madt.c */
}

static _Alignas(16) struct {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) idt_ptr;

void arch_processor_reset(void)
{
	idt_ptr.limit = 0;
	asm volatile("lidt (%0)" ::"r"(&idt_ptr) : "memory");
	asm volatile("int $3");
}

#define MSR_IA32_TME_ACTIVATE 0x982
size_t arch_processor_physical_width(void)
{
	static size_t physical_address_bits = 0;
	if(!physical_address_bits) {
		uint32_t a, b, c, d;
		if(!__get_cpuid_count(0x80000008, 0, &a, &b, &c, &d))
			panic("unable to determine physical address size");
		physical_address_bits = a & 0xff;
	}
	return physical_address_bits - 1;
}

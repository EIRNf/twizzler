#pragma once

struct __attribute__((packed)) x86_64_exception_frame {
	uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi;
	uint64_t int_no, err_code;
	uint64_t rip, cs, rflags, userrsp, ss;
};

struct __attribute__((packed)) x86_64_syscall_frame {
	uint64_t r15, r14, r13, r12, r10, r9, r8, rdx, rsi, rdi, rbx, rax, r11, rcx, rbp, rsp, cs;
};

struct __attribute__((packed)) arch_become_frame {
	uint64_t r15, r14, r13, r12, r10, r9, r8, rdx, rsi, rdi, rbx, rax, r11, rcx, rbp, rsp;
	uint64_t fs, gs;
};

struct kheap_run;
struct arch_thread {
	_Alignas(16) struct x86_64_syscall_frame syscall;
	_Alignas(16) struct x86_64_exception_frame exception;
	struct kheap_run *xsave_run;
	uint64_t fs, gs;
	bool was_syscall;
	bool fpu_init;
};

#define XSAVE_REGION_SIZE 1024

#pragma once
#include <twz/obj.h>
#include <twz/sys.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWZ_GATE_SIZE 32
/*
#define __TWZ_GATE(fn, g)                                                                          \
    __asm__(".section .gates, \"ax\", @progbits\n"                                                 \
            ".global __twz_gate_" #fn "\n"                                                         \
            ".type __twz_gate_" #fn " STT_FUNC\n"                                                  \
            ".org " #g "*32, 0x90\n"                                                               \
            "__twz_gate_" #fn ":\n"                                                                \
            "leaq " #fn "(%rip), %rax\n"                                                           \
            "jmpq *%rax\n"                                                                         \
            "retq\n"                                                                               \
            ".balign 16, 0x90\n"                                                                   \
            ".previous");
__asm__(".section .gates, \"ax\", @progbits\n"
        ".global __twz_gate_bstream_write \n"
        ".type __twz_gate_bstream_write STT_FUNC\n"
        ".org 2*32, 0x90\n"
        "__twz_gate_bstream_write:\n"
        "movabs $bstream_write, %rax\n"
        "leaq -__twz_gate_bstream_write+2*32(%rip), %r15\n"
        "addq %r15, %rax\n"
        "jmp .\n"
        ".balign 32, 0x90\n"
        ".previous");
*/

extern void libtwz_gate_return(long);
extern void *__twz_secapi_nextstack;
#define __TWZ_GATE_SHARED(fn, g)                                                                   \
	__asm__(".section .gates, \"ax\", @progbits\n"                                                 \
	        ".global __twz_gate_" #fn "\n"                                                         \
	        ".type __twz_gate_" #fn " STT_FUNC\n"                                                  \
	        ".org " #g "*32, 0x90\n"                                                               \
	        "__twz_gate_" #fn ":\n"                                                                \
	        "mov $0, %rsp\n"                                                                       \
	        "lock xchgq __twz_secapi_nextstack, %rsp;"                                             \
	        "test %rsp, %rsp;\n"                                                                   \
	        "jz __twz_gate_" #fn "\n"                                                              \
	        "movabs $" #fn ", %rax\n"                                                              \
	        "call *%rax\n"                                                                         \
	        "movq %rax, %rdi\n"                                                                    \
	        "jmp libtwzsec_gate_return\n"                                                          \
	        "retq\n"                                                                               \
	        ".balign 32, 0x90\n"                                                                   \
	        ".previous");

#define __TWZ_GATE(fn, g)                                                                          \
	__asm__(".section .gates, \"ax\", @progbits\n"                                                 \
	        ".global __twz_gate_" #fn "\n"                                                         \
	        ".type __twz_gate_" #fn " STT_FUNC\n"                                                  \
	        ".org " #g "*32, 0x90\n"                                                               \
	        "__twz_gate_" #fn ":\n"                                                                \
	        "movabs $" #fn ", %rax\n"                                                              \
	        "leaq -__twz_gate_" #fn "+" #g "*32(%rip), %r10\n"                                     \
	        "addq %r10, %rax\n"                                                                    \
	        "jmpq *%rax\n"                                                                         \
	        "retq\n"                                                                               \
	        ".balign 32, 0x90\n"                                                                   \
	        ".previous");

#define TWZ_GATE(fn, g) __TWZ_GATE(fn, g)
#define TWZ_GATE_SHARED(fn, g) __TWZ_GATE_SHARED(fn, g)
#define TWZ_GATE_OFFSET (OBJ_NULLPAGE_SIZE + 0x200)

#define TWZ_GATE_CALL(_obj, g)                                                                     \
	({                                                                                             \
		twzobj *obj = _obj;                                                                        \
		(void *)((obj ? (uintptr_t)twz_object_base(obj) - OBJ_NULLPAGE_SIZE : 0ull)                \
		         + g * TWZ_GATE_SIZE + TWZ_GATE_OFFSET);                                           \
	})

struct secure_api_header {
	objid_t sctx;
	objid_t view;
};

struct secure_api {
	struct secure_api_header *hdr;
	twzobj obj;
	const char *name;
};

void twz_secure_api_setup_tmp_stack(void);
static inline void *twz_secure_api_alloc_stackarg(size_t size, size_t *ctx)
{
	/* TODO: alignment */
	twz_secure_api_setup_tmp_stack();
	if(*ctx == 0) {
		*ctx = 0x400000;
	}
	*ctx -= size;
	return (void *)((TWZSLOT_TMPSTACK * OBJ_MAXSIZE) + *ctx);
}

static inline int twz_secure_api_open_name(const char *name, struct secure_api *api)
{
	api->hdr = NULL;
	int r = twz_object_init_name(&api->obj, name, FE_READ);
	if(r) {
		return r;
	}
	api->hdr = (struct secure_api_header *)twz_object_base(&api->obj);
	api->name = strdup(name);
	return 0;
}

static inline void twz_secure_api_close(struct secure_api *api)
{
	if(api->hdr) {
		free((void *)api->name);
		twz_object_release(&api->obj);
		api->hdr = NULL;
	}
}

#define twz_secure_api_call1(hdr, gate, arg)                                                       \
	({                                                                                             \
		twz_secure_api_setup_tmp_stack();                                                          \
		struct sys_become_args args = {                                                            \
			.target_view = hdr->view,                                                              \
			.target_rip = (uint64_t)TWZ_GATE_CALL(NULL, gate),                                     \
			.rdi = (long)arg,                                                                      \
			.rsp = (TWZSLOT_TMPSTACK * OBJ_MAXSIZE + 0x200000),                                    \
		};                                                                                         \
		long r = sys_attach(0, hdr->sctx, 0, KSO_SECCTX);                                          \
		if(r == 0)                                                                                 \
			r = sys_become(&args, 0, 0);                                                           \
		r;                                                                                         \
	})

#define twz_secure_api_call3(hdr, gate, arg1, arg2, arg3)                                          \
	({                                                                                             \
		twz_secure_api_setup_tmp_stack();                                                          \
		struct sys_become_args args = {                                                            \
			.target_view = hdr->view,                                                              \
			.target_rip = (uint64_t)TWZ_GATE_CALL(NULL, gate),                                     \
			.rdx = (long)arg3,                                                                     \
			.rdi = (long)arg1,                                                                     \
			.rsi = (long)arg2,                                                                     \
			.rsp = (TWZSLOT_TMPSTACK * OBJ_MAXSIZE + 0x200000),                                    \
		};                                                                                         \
		long r = sys_attach(0, hdr->sctx, 0, KSO_SECCTX);                                          \
		if(r == 0)                                                                                 \
			r = sys_become(&args, 0, 0);                                                           \
		r;                                                                                         \
	})

#define DECLARE_SAPI_ENTRY(name, gate, ret_type, ...)                                              \
	TWZ_GATE_SHARED(__sapi_entry_##name, gate);                                                    \
	ret_type __sapi_entry_##name(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

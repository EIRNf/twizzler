#pragma once
#include <arch/thread.h>
#include <krc.h>
#include <lib/inthash.h>
#include <lib/list.h>
#include <memory.h>
#include <thread-bits.h>
#include <time.h>
#include <workqueue.h>

#include <twz/_fault.h>
#include <twz/_thrd.h>

struct processor;

enum thread_state {
	THREADSTATE_RUNNING,
	THREADSTATE_BLOCKED,
	THREADSTATE_EXITED,
	THREADSTATE_INITING,
};

#define MAX_SC TWZ_THRD_MAX_SCS

struct thread_become_frame {
	struct arch_become_frame frame;
	struct list entry;
	struct object *view;
};

struct sleep_entry {
	struct thread *thr;
	struct syncpoint *sp;
	struct list entry;
	bool active;
};

struct thread_sctx_entry {
	struct sctx *context;
	struct sctx *backup;
	uint32_t attr, backup_attr;
};

struct thread {
	struct arch_thread arch;
	struct spinlock lock;
	unsigned long id;
	enum thread_state state;
	uint64_t timeslice_expire;
	int priority;
	_Atomic bool page_alloc;
	// struct krc refs;
	objid_t thrid;

	struct processor *processor;
	struct vm_context *ctx;

	struct spinlock sc_lock;
	struct sctx *active_sc;
	struct thread_sctx_entry *sctx_entries;

	struct kso_throbj *throbj;
	int kso_attachment_num;

	struct list rq_entry, all_entry;
	struct sleep_entry *sleep_entries;
	size_t sleep_count;
	struct task free_task;
	/* pager info */
	objid_t pager_obj_req;
	ssize_t pager_page_req;
	uintptr_t _last_oaddr; // TODO: remove
	uint32_t _last_flags;
	uint32_t _last_count;
	void *pending_fault_info;
	int pending_fault;
	size_t pending_fault_infolen;
	struct timer sleep_timer;

	struct list become_stack;
	struct object *thrctrl;
};

struct arch_syscall_become_args;
void arch_thread_become(struct arch_syscall_become_args *ba, struct thread_become_frame *, bool);
void arch_thread_become_restore(struct thread_become_frame *frame);
void thread_sleep(struct thread *t, int flags);
void thread_wake(struct thread *t);
void thread_exit(void);
void thread_raise_fault(struct thread *t, int fault, void *info, size_t);
struct timespec;
long thread_sync_single(int operation, long *addr, long arg, bool);
long thread_wake_object(struct object *obj, size_t offset, long arg);
void thread_sync_uninit_thread(struct thread *thr);
long thread_sleep_on_object(struct object *obj, size_t offset, long arg, bool dont_check);
void thread_print_all_threads(void);
void thread_queue_fault(struct thread *thr, int fault, void *info, size_t infolen);
void arch_thread_raise_call(struct thread *t, void *addr, long a0, void *, size_t);

struct thread *thread_lookup(unsigned long id);
struct thread *thread_create(void);
void arch_thread_init(struct thread *thread,
  void *entry,
  void *arg,
  void *stack,
  size_t stacksz,
  void *tls,
  size_t);
void arch_thread_destroy(struct thread *thread);

void thread_initialize_processor(struct processor *proc);

long arch_thread_syscall_num(void);
void thread_schedule_resume(void);
void thread_schedule_resume_proc(struct processor *proc);
void arch_thread_resume(struct thread *thread, uint64_t timeout);
uintptr_t arch_thread_instruction_pointer(void);

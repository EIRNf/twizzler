#pragma once

#include <arch/processor.h>
#include <interrupt.h>
#include <lib/list.h>
#include <thread.h>
#include <workqueue.h>

#define PROCESSOR_UP 1
#define PROCESSOR_BSP 4
#define PROCESSOR_REGISTERED 8
#define PROCESSOR_HASWORK 16
#define PROCESSOR_HALTING 32

#define PROCESSOR_IPI_BARRIER 1
#define PROCESSOR_IPI_NOWAIT 2

#define PROCESSOR_INITIALIZER_ORDER 0

#ifdef CONFIG_MAX_CPUS
#define PROCESSOR_MAX_CPUS CONFIG_MAX_CPUS
#else
#define PROCESSOR_MAX_CPUS 64
#endif

#include <twz/sys/dev/processor.h>
struct processor {
	struct arch_processor arch;
	struct list runqueue;
	struct spinlock sched_lock;
	char pad[64];
	_Atomic int flags;
	char pad2[64];
	unsigned int id;
	unsigned long load;
	void *percpu;
	struct proc_stats stats;
	struct object *obj;
	struct workqueue wq;

	long ctr;
};

static inline bool processor_has_threads(struct processor *proc)
{
	return !list_empty(&proc->runqueue);
}

void processor_perproc_init(struct processor *proc);
void processor_percpu_regions_init(void);
void processor_early_init(void);

void processor_register(bool bsp, unsigned int id);
void arch_processor_enumerate(void);
bool arch_processor_boot(struct processor *proc);
void arch_processor_reset(void);
uint64_t arch_processor_get_nanoseconds(void);
size_t arch_processor_physical_width(void);
size_t arch_processor_virtual_width(void);
void processor_attach_thread(struct processor *proc, struct thread *thread);
void arch_processor_init(struct processor *proc);
void arch_processor_early_init(struct processor *proc);
void processor_init_secondaries(void);
void processor_barrier(_Atomic unsigned int *here);

/* this attribute is only valid if we assume we do not get swapped out to different processors
 * within a single thread of execution. This is true, currently, as the only time the scheduler can
 * run is on return from interrupt. We can restrict this further by saying "you may not change a
 * thread's CPU unless it's returning to userspace". */
__attribute__((const)) struct processor *processor_get_current(void);
unsigned int arch_processor_current_id(void);
void processor_send_ipi(int destid, int vector, void *arg, int flags);
void arch_processor_send_ipi(int destid, int vector, int flags);
void arch_processor_scheduler_wakeup(struct processor *proc);
void processor_ipi_finish(void);
void processor_shutdown(void);
void processor_print_all_stats(void);
void processor_print_stats(struct processor *proc);
void processor_update_stats(void);

#define current_processor processor_get_current()

#define DECLARE_PER_CPU(type, name)                                                                \
	__attribute__((section(".data.percpu"))) type __per_cpu_var_##name

#define PTR_ADVANCE(ptr, off)                                                                      \
	({                                                                                             \
		uintptr_t p = (uintptr_t)(ptr);                                                            \
		(typeof(ptr))(p + (off));                                                                  \
	})

#define __per_cpu_var_lea(name, proc)                                                              \
	PTR_ADVANCE(                                                                                   \
	  &__per_cpu_var_##name, (uintptr_t)((proc)->percpu ? proc->percpu : bsp_percpu_region))

#define per_cpu_get(name) __per_cpu_var_lea(name, current_processor)
extern void *bsp_percpu_region;

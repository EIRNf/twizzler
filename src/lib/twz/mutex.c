#include <twz/debug.h>
#include <twz/mutex.h>
#include <twz/sys/sys.h>

_Atomic uint64_t _twz_rcode = 0;
void mutex_acquire(struct mutex *m)
{
	if(!_twz_rcode) {
		uint64_t r = sys_kconf(KCONF_RDRESET, 0);
		/* this code will always be the same within the same power cycle. We can afford a few
		 * overwrites. */
		_twz_rcode = r;
	}

	if(m->resetcode != _twz_rcode) {
		/* might need to bust the lock. Make sure everyone still comes in here until we're done by
		 * changing the resetcode to -1. If we exchange and get 1, we wait. If we don't get -1,
		 * then we're the one that's going to bust the lock. */
		if(atomic_exchange(&m->resetcode, ~0ul) != ~0ul) {
			/* bust lock, and then store new reset code */
			atomic_store(&m->sleep, 0);
			atomic_store(&m->resetcode, _twz_rcode);
		} else {
			debug_printf(
			  "waiting for lock busting? %lx %lx %lx\n", m->resetcode, _twz_rcode, m->sleep);
			while(atomic_load(&m->resetcode) == ~0ul)
				asm("pause");
		}
	}
	/* try to grab the lock by spinning */
	uint_least64_t v;
	for(int i = 0; i < 100; i++) {
		v = 0;
		if(atomic_compare_exchange_strong(&m->sleep, &v, 1)) {
			return;
		}
		asm("pause");
	}
	/* indicate waiting */
	if(v)
		v = atomic_exchange(&m->sleep, 2);

	struct sys_thread_sync_args args = {
		.op = THREAD_SYNC_SLEEP,
		.addr = (uint64_t *)&m->sleep,
		.arg = 2,
	};

	/* actually sleep for the lock */
	while(v) {
		if(sys_thread_sync(1, &args, NULL) < 0) {
			libtwz_panic("mutex_acquire thread sync error");
		}

		v = atomic_exchange(&m->sleep, 2);
	}
}

void mutex_release(struct mutex *m)
{
	if(atomic_load(&m->sleep) == 2) {
		atomic_store(&m->sleep, 0);
	} else if(atomic_exchange(&m->sleep, 0) == 1) {
		/* no-one was waiting! */
		return;
	}

	/* try to downgrade to non-waiting case */
	for(int i = 0; i < 100; i++) {
		uint_least64_t v = 1;
		if(atomic_load(&m->sleep)) {
			if(atomic_compare_exchange_strong(&m->sleep, &v, 2))
				return;
		}
		asm("pause");
	}
	/* wake up anyone who is waiting */
	struct sys_thread_sync_args args = {
		.op = THREAD_SYNC_WAKE,
		.addr = (uint64_t *)&m->sleep,
		.arg = 1,
	};
	if(sys_thread_sync(1, &args, NULL) < 0) {
		libtwz_panic("mutex_acquire thread sync error");
	}
}

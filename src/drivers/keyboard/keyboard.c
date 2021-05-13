/*
 * SPDX-FileCopyrightText: 2021 Daniel Bittman <danielbittman1@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <twz/debug.h>
#include <twz/meta.h>
#include <twz/name.h>
#include <twz/obj.h>
#include <twz/obj/bstream.h>
#include <twz/sys/dev/device.h>
#include <twz/sys/sys.h>
#include <twz/sys/thread.h>

static twzobj ks_obj;
static twzobj us_obj;

static struct device_repr *dr;

ssize_t get_input(char *buf, size_t len)
{
	size_t c = 0;
	while(c < len) {
		long x = atomic_exchange(&dr->syncs[0], 0);
		if(!x) {
			if(c) {
				return c;
			}
			struct sys_thread_sync_args args = {
				.op = THREAD_SYNC_SLEEP,
				.arg = 0,
				.addr = (uint64_t *)&dr->syncs[0],
			};

			int r = sys_thread_sync(1, &args, NULL);
			if(r)
				return r;
		} else {
			buf[c++] = x;
		}
	}
	return c;
}

int main(int argc, char **argv)
{
	if(argc < 3)
		abort();
	int r;
	char *kernel_side = argv[1];
	char *user_side = argv[2];

	if(!kernel_side || !user_side)
		abort();

	if((r = twz_object_init_name(&ks_obj, kernel_side, FE_READ | FE_WRITE))) {
		fprintf(stderr, "failed to open kernel-side keyboard object\n");
		return 1;
	}

	if((r = twz_object_init_name(&us_obj, user_side, FE_READ | FE_WRITE))) {
		fprintf(stderr, "failed to open user-side keyboard object\n");
		return 1;
	}
	// objid_t ksid, usid;
	// objid_parse(kernel_side, strlen(kernel_side), &ksid);
	// objid_parse(user_side, strlen(user_side), &usid);

	// twz_object_init_guid(&ks_obj, ksid, FE_READ | FE_WRITE);
	// twz_object_init_guid(&us_obj, usid, FE_READ | FE_WRITE);

	dr = twz_object_base(&ks_obj);

	if(sys_thrd_ctl(THRD_CTL_SET_IOPL, 3)) {
		fprintf(stderr, "failed to set IOPL to 3\n");
		abort();
	}

	for(;;) {
		char buf[128];
		ssize_t r = get_input(buf, 127);
		if(r < 0) {
			fprintf(stderr, "ERR!: %d\n", (int)r);
			return 1;
		}
		size_t count = 0;
		ssize_t w = 0;
		do {
			w = twzio_write(&us_obj, buf + count, r - count, 0, 0);
			if(w < 0)
				break;
			count += w;
		} while(count < (size_t)r);

		if(w < 0) {
			fprintf(stderr, "ERR!: %d\n", (int)w);
			return 1;
		}
	}
	return 0;
}

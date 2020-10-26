#include <stdbool.h>
#include <twix/twix.h>
#include <twz/debug.h>

#include "../syscalls.h"

struct unix_server {
	twzobj cmdqueue, buffer;
	struct secure_api api;
	_Atomic bool inited;
	_Atomic bool ok;
};

static struct unix_server userver = {};

void twix_sync_command(struct twix_queue_entry *tqe)
{
	queue_submit(&userver.cmdqueue, (struct queue_entry *)tqe, 0);
	queue_get_finished(&userver.cmdqueue, (struct queue_entry *)tqe, 0);
}

struct twix_queue_entry build_tqe(enum twix_command cmd, int flags, size_t bufsz, ...)
{
	int nr_va = 0;
	switch(cmd) {
		case TWIX_CMD_GET_PROC_INFO:
			nr_va = 0;
			break;
		case TWIX_CMD_REOPEN_V1_FD:
			nr_va = 5;
			break;
		case TWIX_CMD_OPENAT:
			nr_va = 3;
			break;
		default:
			nr_va = 0;
			break;
	}
	long args[6] = {};
	va_list va;
	va_start(va, bufsz);
	for(int i = 0; i < nr_va; i++) {
		args[i] = va_arg(va, long);
	}
	va_end(va);

	struct twix_queue_entry tqe = {
		.cmd = cmd,
		.arg0 = args[0],
		.arg1 = args[1],
		.arg2 = args[2],
		.arg3 = args[3],
		.arg4 = args[4],
		.arg5 = args[5],
		.buflen = bufsz,
		.flags = flags,
	};

	return tqe;
}

void extract_bufdata(void *ptr, size_t len)
{
	void *base = twz_object_base(&userver.buffer);
	memcpy(ptr, base, len);
}

void write_bufdata(const void *ptr, size_t len)
{
	void *base = twz_object_base(&userver.buffer);
	memcpy(base, ptr, len);
}

long get_proc_info(struct proc_info *info)
{
	struct twix_queue_entry tqe = build_tqe(TWIX_CMD_GET_PROC_INFO, 0, sizeof(struct proc_info));
	twix_sync_command(&tqe);
	extract_bufdata(info, sizeof(*info));

	return 0;
}

static bool setup_queue(void)
{
	if(userver.inited)
		return userver.ok;
	userver.ok = false;
	userver.inited = true;
	if(twz_secure_api_open_name("/dev/unix", &userver.api)) {
		return false;
	}
	objid_t qid, bid;
	int r = twix_open_queue(&userver.api, 0, &qid, &bid);
	if(r) {
		return false;
	}

	if(twz_object_init_guid(&userver.cmdqueue, qid, FE_READ | FE_WRITE))
		return false;

	if(twz_object_init_guid(&userver.buffer, bid, FE_READ | FE_WRITE))
		return false;

	userver.ok = true;
	userver.inited = true;

	for(int fd = 0; fd < MAX_FD; fd++) {
		struct file *file = twix_get_fd(fd);
		if(file) {
			struct twix_queue_entry tqe = build_tqe(TWIX_CMD_REOPEN_V1_FD,
			  0,
			  0,
			  file->fd,
			  ID_LO(twz_object_guid(&file->obj)),
			  ID_HI(twz_object_guid(&file->obj)),
			  file->fcntl_fl,
			  file->pos);
			twix_sync_command(&tqe);
		}
	}

	return true;
}

struct syscall_args {
	long a0, a1, a2, a3, a4, a5;
	long num;
	struct twix_register_frame *frame;
};

#include "../syscall_defs.h"

long hook_proc_info_syscalls(struct syscall_args *args)
{
	struct proc_info info = {};
	get_proc_info(&info);
	switch(args->num) {
		case LINUX_SYS_getpid:
			debug_printf("GETPID: %d\n", info.pid);
			return info.pid;
			break;
		default:
			return -ENOSYS;
	}
}

static long __dummy(struct syscall_args *args __attribute__((unused)))
{
	return 0;
}

long hook_open(struct syscall_args *args)
{
	const char *path = (const char *)args->a0;
	struct twix_queue_entry tqe =
	  build_tqe(TWIX_CMD_OPENAT, 0, strlen(path), 0, args->a1, args->a2);
	write_bufdata(path, strlen(path) + 1);
	twix_sync_command(&tqe);
	return tqe.ret;
}

static long (*syscall_v2_table[1024])(struct syscall_args *) = {
	[LINUX_SYS_getpid] = hook_proc_info_syscalls,
	[LINUX_SYS_set_tid_address] = __dummy,
	[LINUX_SYS_open] = hook_open,
};

extern const char *syscall_names[];

bool try_twix_version2(struct twix_register_frame *frame,
  long num,
  long a0,
  long a1,
  long a2,
  long a3,
  long a4,
  long a5,
  long *ret)
{
	struct syscall_args args = {
		.a0 = a0,
		.a1 = a1,
		.a2 = a2,
		.a3 = a3,
		.a4 = a4,
		.a5 = a5,
		.num = num,
		.frame = frame,
	};
	if(!setup_queue())
		return false;
	if(num >= 1024 || num < 0 || !syscall_v2_table[num]) {
		twix_log("twix_v2 syscall: %ld (%s)\n", num, syscall_names[num]);
		return false;
	}
	*ret = syscall_v2_table[num](&args);
	return *ret != -ENOSYS;
}

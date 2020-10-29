// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include "test.h"

#define sleep_ns 87654321
static int lkl_test_nanosleep(void)
{
	struct lkl_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = sleep_ns,
	};
	struct timespec start, stop;
	long delta;
	long ret;

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = lkl_sys_nanosleep((struct __lkl__kernel_timespec *)&ts, NULL);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	delta = 1e9*(stop.tv_sec - start.tv_sec) +
		(stop.tv_nsec - start.tv_nsec);

	lkl_test_logf("sleep %ld, expected sleep %d\n", delta, sleep_ns);

	if (ret == 0 && delta > sleep_ns * 0.9)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

LKL_TEST_CALL(getpid, lkl_sys_getpid, 1)

void check_latency(long (*f)(void), long *min, long *max, long *avg)
{
	int i;
	struct timespec start, stop;
	unsigned long long sum = 0;
	static const int count = 20;
	long delta;

	*min = 1000000000;
	*max = -1;

	for (i = 0; i < count; i++) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		f();
		clock_gettime(CLOCK_MONOTONIC, &stop);

		delta = 1e9*(stop.tv_sec - start.tv_sec) +
			(stop.tv_nsec - start.tv_nsec);

		if (*min > delta)
			*min = delta;
		if (*max < delta)
			*max = delta;
		sum += delta;
	}
	*avg = sum / count;
}

static long native_getpid(void)
{
	getpid();
	return 0;
}

int lkl_test_syscall_latency(void)
{
	long min, max, avg;

	lkl_test_logf("avg/min/max: ");

	check_latency(lkl_sys_getpid, &min, &max, &avg);

	lkl_test_logf("lkl:%ld/%ld/%ld ", avg, min, max);

	check_latency(native_getpid, &min, &max, &avg);

	lkl_test_logf("native:%ld/%ld/%ld\n", avg, min, max);

	return TEST_SUCCESS;
}

#define access_rights 0721

LKL_TEST_CALL(creat, lkl_sys_creat, 3, "/file", access_rights)
LKL_TEST_CALL(close, lkl_sys_close, 0, 0);
LKL_TEST_CALL(failopen, lkl_sys_open, -LKL_ENOENT, "/file2", 0, 0);
LKL_TEST_CALL(umask, lkl_sys_umask, 022,  0777);
LKL_TEST_CALL(umask2, lkl_sys_umask, 0777, 0);
LKL_TEST_CALL(open, lkl_sys_open, 0, "/file", LKL_O_RDWR, 0);
static const char wrbuf[] = "test";
LKL_TEST_CALL(write, lkl_sys_write, sizeof(wrbuf), 0, wrbuf, sizeof(wrbuf));
LKL_TEST_CALL(lseek_cur, lkl_sys_lseek, sizeof(wrbuf), 0, 0, LKL_SEEK_CUR);
LKL_TEST_CALL(lseek_end, lkl_sys_lseek, sizeof(wrbuf), 0, 0, LKL_SEEK_END);
LKL_TEST_CALL(lseek_set, lkl_sys_lseek, 0, 0, 0, LKL_SEEK_SET);

int lkl_test_read(void)
{
	char buf[10] = { 0, };
	long ret;

	ret = lkl_sys_read(0, buf, sizeof(buf));

	lkl_test_logf("lkl_sys_read=%ld buf=%s\n", ret, buf);

	if (ret == sizeof(wrbuf) && !strcmp(wrbuf, buf))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int lkl_test_fstat(void)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_fstat(0, (void *)&stat);

	lkl_test_logf("lkl_sys_fstat=%ld mode=%o size=%ld\n", ret, stat.st_mode,
		      stat.st_size);

	if (ret == 0 && stat.st_size == sizeof(wrbuf) &&
	    stat.st_mode == (access_rights | LKL_S_IFREG))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

LKL_TEST_CALL(mkdir, lkl_sys_mkdir, 0, "/proc", access_rights)

int lkl_test_stat(void)
{
	struct lkl_stat stat;
	long ret;

	ret = lkl_sys_stat("/proc", (void *)&stat);

	lkl_test_logf("lkl_sys_stat(\"/proc\")=%ld mode=%o\n", ret,
		      stat.st_mode);

	if (ret == 0 && stat.st_mode == (access_rights | LKL_S_IFDIR))
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int lkl_test_pipe2(void)
{
	int pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	static const char msg[] = "Hello world!";
	char str[20];
	int msg_len_bytes = strlen(msg) + 1;
	int cmp_res;
	long ret;

	ret = lkl_sys_pipe2(pipe_fds, LKL_O_NONBLOCK);
	if (ret) {
		lkl_test_logf("pipe2: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			lkl_test_logf("write error: %s\n", lkl_strerror(ret));
		else
			lkl_test_logf("short write: %ld\n", ret);
		return TEST_FAILURE;
	}

	ret = lkl_sys_read(pipe_fds[READ_IDX], str, msg_len_bytes);
	if (ret != msg_len_bytes) {
		if (ret < 0)
			lkl_test_logf("read error: %s\n", lkl_strerror(ret));
		else
			lkl_test_logf("short read: %ld\n", ret);
		return TEST_FAILURE;
	}

	cmp_res = memcmp(msg, str, msg_len_bytes);
	if (cmp_res) {
		lkl_test_logf("memcmp failed: %d\n", cmp_res);
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[0]);
	if (ret) {
		lkl_test_logf("close error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	ret = lkl_sys_close(pipe_fds[1]);
	if (ret) {
		lkl_test_logf("close error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

static int lkl_test_epoll(void)
{
	int epoll_fd, pipe_fds[2];
	int READ_IDX = 0, WRITE_IDX = 1;
	struct lkl_epoll_event wait_on, read_result;
	static const char msg[] = "Hello world!";
	long ret;

	memset(&wait_on, 0, sizeof(wait_on));
	memset(&read_result, 0, sizeof(read_result));

	ret = lkl_sys_pipe2(pipe_fds, LKL_O_NONBLOCK);
	if (ret) {
		lkl_test_logf("pipe2 error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	epoll_fd = lkl_sys_epoll_create(1);
	if (epoll_fd < 0) {
		lkl_test_logf("epoll_create error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	wait_on.events = LKL_POLLIN | LKL_POLLOUT;
	wait_on.data = pipe_fds[READ_IDX];

	ret = lkl_sys_epoll_ctl(epoll_fd, LKL_EPOLL_CTL_ADD, pipe_fds[READ_IDX],
				&wait_on);
	if (ret < 0) {
		lkl_test_logf("epoll_ctl error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* Shouldn't be ready before we have written something */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 0) {
		if (ret < 0)
			lkl_test_logf("epoll_wait error: %s\n",
				      lkl_strerror(ret));
		else
			lkl_test_logf("epoll_wait: bad event: 0x%lx\n", ret);
		return TEST_FAILURE;
	}

	ret = lkl_sys_write(pipe_fds[WRITE_IDX], msg, strlen(msg) + 1);
	if (ret < 0) {
		lkl_test_logf("write error: %s\n", lkl_strerror(ret));
		return TEST_FAILURE;
	}

	/* We expect exactly 1 fd to be ready immediately */
	ret = lkl_sys_epoll_wait(epoll_fd, &read_result, 1, 0);
	if (ret != 1) {
		if (ret < 0)
			lkl_test_logf("epoll_wait error: %s\n",
				      lkl_strerror(ret));
		else
			lkl_test_logf("epoll_wait: bad ev no %ld\n", ret);
		return TEST_FAILURE;
	}

	/* Already tested reading from pipe2 so no need to do it
	 * here
	 */

	return TEST_SUCCESS;
}

LKL_TEST_CALL(chdir_proc, lkl_sys_chdir, 0, "proc");

static int dir_fd;

static int lkl_test_open_cwd(void)
{
	dir_fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	if (dir_fd < 0) {
		lkl_test_logf("failed to open current directory: %s\n",
			      lkl_strerror(dir_fd));
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

/* column where to insert a line break for the list file tests below. */
#define COL_LINE_BREAK 70

static int lkl_test_getdents64(void)
{
	long ret;
	char buf[1024], *pos;
	struct lkl_linux_dirent64 *de;
	int wr;

	de = (struct lkl_linux_dirent64 *)buf;
	ret = lkl_sys_getdents64(dir_fd, de, sizeof(buf));

	wr = lkl_test_logf("%d ", dir_fd);

	if (ret < 0)
		return TEST_FAILURE;

	for (pos = buf; pos - buf < ret; pos += de->d_reclen) {
		de = (struct lkl_linux_dirent64 *)pos;

		wr += lkl_test_logf("%s ", de->d_name);
		if (wr >= COL_LINE_BREAK) {
			lkl_test_logf("\n");
			wr = 0;
		}
	}

	return TEST_SUCCESS;
}

LKL_TEST_CALL(close_dir_fd, lkl_sys_close, 0, dir_fd);
LKL_TEST_CALL(chdir_root, lkl_sys_chdir, 0, "/");
LKL_TEST_CALL(mount_fs_proc, lkl_sys_mount, 0, "none", "/proc", "proc", 0,
	      NULL);
LKL_TEST_CALL(umount_fs_proc, lkl_sys_umount, 0, "/proc", 0);

LKL_TEST_CALL(start_kernel, lkl_start_kernel, 0, &lkl_host_ops,
	     "mem=16M loglevel=8");
LKL_TEST_CALL(stop_kernel, lkl_sys_halt, 0);

static struct lkl_test tests[] = {
	LKL_TEST(start_kernel),
	LKL_TEST(getpid),
	LKL_TEST(syscall_latency),
	LKL_TEST(umask),
	LKL_TEST(umask2),
	LKL_TEST(creat),
	LKL_TEST(close),
	LKL_TEST(failopen),
	LKL_TEST(open),
	LKL_TEST(write),
	LKL_TEST(lseek_cur),
	LKL_TEST(lseek_end),
	LKL_TEST(lseek_set),
	LKL_TEST(read),
	LKL_TEST(fstat),
	LKL_TEST(mkdir),
	LKL_TEST(stat),
	LKL_TEST(nanosleep),
	LKL_TEST(pipe2),
	LKL_TEST(epoll),
	LKL_TEST(mount_fs_proc),
	LKL_TEST(chdir_proc),
	LKL_TEST(open_cwd),
	LKL_TEST(getdents64),
	LKL_TEST(close_dir_fd),
	LKL_TEST(chdir_root),
	LKL_TEST(umount_fs_proc),
	LKL_TEST(stop_kernel),
};

int main(int argc, const char **argv)
{
	return lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			    "boot");
}

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <lkl_host.h>

static int epollfd = -1;
#define MAX_EPOLL_EVENTS 64

static void *epoll_thread(void *data)
{
	int ret, irq;
	struct epoll_event events[MAX_EPOLL_EVENTS];

	memset(events, 0, sizeof(struct epoll_event) * MAX_EPOLL_EVENTS);

	while (1) {
		ret = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, 0);
		if (ret < 0) {
			fprintf(stderr, "epoll_wait: %d", errno);
		}

		for (int i = 0; i < MAX_EPOLL_EVENTS; i++) {
			if ((events[i].events & EPOLLIN) ||
			    (events[i].events & EPOLLOUT)) {
				irq = (int)(long)events[i].data.ptr;
				lkl_trigger_irq(irq);
			}
		}
	}
	return NULL;
}


static int register_irq_fd(int irq, int fd, int type, void *dev_id)
{
	struct epoll_event event;
	int result, flags;
	pthread_t th;

	if (epollfd == -1) {
		epollfd = epoll_create(MAX_EPOLL_EVENTS);
		pthread_create(&th, NULL, epoll_thread, NULL);
	}

	/* make fd non-block */
	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -errno;

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		fprintf(stderr, "fcntl: failed to set O_ASYNC and O_NONBLOCK on fd # %d", errno);
		return -errno;
	}

#define IRQ_READ  0
#define IRQ_WRITE 1
#define IRQ_NONE 2
	event.data.ptr = (void *)(long)irq;
	if (type == IRQ_READ)
		event.events = EPOLLIN | EPOLLPRI | EPOLLET;
	if (type == IRQ_WRITE)
		event.events = EPOLLOUT | EPOLLET;
	result = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

	return result;
}

static void unregister_irq_fd(int irq, int fd)
{
	struct epoll_event event;
	int result;

	result = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event);
	if (result < 0)
		fprintf(stderr, "epoll_ctl:CTL_DEL: %d", errno);
	return;
}

struct lkl_host_operations lkl_host_ops = {
	.register_irq_fd = register_irq_fd,
	.unregister_irq_fd = unregister_irq_fd,
};

#include <stdio.h>
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

int main(char *argv[])
{
	void *addr, *addr2;
	size_t pagesize = getpagesize();
	int fd;

	/* munmap shrink test */
	for (int i = 0; i < 4; i++) {
		addr = mmap(NULL, pagesize * 4, PROT_READ | PROT_WRITE,
			    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (addr == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}

		munmap(addr + pagesize * i, pagesize);
		printf("memory %p is finely unmapped at %p\n",
		       addr, addr + pagesize * i);
	}

	/* mremap shrink test */
	addr = mmap(NULL, pagesize * 4 - 1, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	addr2 = mremap(addr, pagesize * 4 - 1, pagesize * 4 - 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped at %p\n", addr, addr2);

	addr2 = mremap(addr, pagesize * 4 -2 , pagesize * 4, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped at %p\n", addr, addr2);

	addr2 = mremap(addr, pagesize * 4, pagesize * 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped at %p\n", addr, addr2);

	addr2 = mremap(addr2, pagesize * 2, pagesize * 2 - 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped at %p\n", addr, addr2);

	addr2 = mremap(addr2, pagesize * 2 - 2, pagesize * 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped at %p\n", addr, addr2);

	/* file-mapped mremap shrink test */
	fd = open("/tmp/testfile", O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	addr = mmap(NULL, pagesize * 4, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	addr2 = mremap(addr, pagesize * 4, pagesize * 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	}
	printf("memory %p is finely remapped (file) at %p\n", addr, addr2);

	/* non anonymous file map */
	addr = mmap(NULL, pagesize * 4, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	addr2 = mremap(addr, pagesize * 4, pagesize * 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		if (errno == EINVAL)
			printf("memory %p isn't remapped (file) at %p (expected)\n", addr, addr2);
		else
			printf("memory %p is unexpectedly failed remapped (file) at %p\n", addr, addr2);
	} else {
		printf("memory %p is unexpectedly remapped (file) at %p\n", addr, addr2);
	}

	/* anonymous map with file (/dev/zero) */
	close(fd);
	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	addr = mmap(NULL, pagesize * 4, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	addr2 = mremap(addr, pagesize * 4, pagesize * 2, MREMAP_MAYMOVE);
	if (addr2 == MAP_FAILED) {
		perror("mremap");
		exit(1);
	} else {
		printf("memory %p is expectedly remapped (file-anon-zero) at %p\n", addr, addr2);
	}

	sleep(10);
}

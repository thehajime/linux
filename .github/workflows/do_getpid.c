#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <assert.h>


#ifndef BENCH_LDPRELOAD
extern pid_t do_getpid(void);

void __do_getpid(void)
{
	asm volatile (".globl do_getpid");
	asm volatile ("do_getpid:");
	asm volatile ("movq $39, %rax");
	asm volatile ("syscall");
	asm volatile ("ret");
}
#else
pid_t do_getpid(void)
{
	return getpid();
}
#endif

int main(int argc, char* const* argv)
{
	int ch;
	unsigned long loopcnt = 0;

	while ((ch = getopt(argc, argv, "c:")) != -1) {
		switch (ch) {
		case 'c':
			loopcnt = atol(optarg);
			break;

		default:
			printf("unknown option\n");
			exit(1);
		}
	}

	if (!loopcnt) {
		printf("please specify loop count by -c\n");
		exit(0);
	}

	{
		pid_t my_pid = getpid();
		{
			unsigned long t;
			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				t = ts.tv_sec * 1000000000UL + ts.tv_nsec;
			}
			{
				unsigned long i;
				for (i = 0; i < loopcnt; i++)
					assert(my_pid == do_getpid());
			}
			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				t = ts.tv_sec * 1000000000UL + ts.tv_nsec - t;
			}
			printf("average %5lu nsec\n", t / loopcnt);
		}
	}

	return 0;
}

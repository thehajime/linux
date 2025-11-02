#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <sys/time.h>


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

	unsigned long t;
	struct timespec ts;
	struct timeval tv;

	unsigned long i, j;
	for (j = 0; j < 2; j++) {
		clock_gettime(CLOCK_REALTIME, &ts);
		t = ts.tv_sec * 1000000000UL + ts.tv_nsec;

		for (i = 0; i < loopcnt; i++) {
			if (j == 0)
				clock_gettime(CLOCK_REALTIME, &ts);
			else
				gettimeofday(&tv, NULL);
		}

		clock_gettime(CLOCK_REALTIME, &ts);
		t = ts.tv_sec * 1000000000UL + ts.tv_nsec - t;
		printf("average[%s] %5lu nsec\n", j == 0 ? "clock_gettime"
		       : "gettimeofday", t / loopcnt);
	}


	return 0;
}

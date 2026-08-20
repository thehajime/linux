#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* this function is run by the second thread */
void *inc_x(void *x_void_ptr)
{
	/* Allow this thread to be cancelled even if it's in a syscall */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	printf("set cancel\n");

	/* increment x to 100 */
	int *x_ptr = (int *)x_void_ptr;

	while(++(*x_ptr) < 100);

	printf("x increment finished\n");

	/* the function must return something - NULL will do */
	return NULL;

}

#define NUM_LOOP 1000
int main(int argc, char *argv[])
{

	int x = 0, y = 0, i = 0, num_loop = NUM_LOOP;

	/* show the initial values of x and y */
	printf("x: %d, y: %d\n", x, y);

	/* this variable is our reference to the second thread */
	pthread_t inc_x_thread;

	if (argc > 1)
		num_loop = atoi(argv[1]);

	while (i < num_loop) {
		/* create a second thread which executes inc_x(&x) */
		if(pthread_create(&inc_x_thread, NULL, inc_x, &x)) {

			fprintf(stderr, "Error creating thread\n");
			return 1;

		}
#if 0
		/* XXX: sometimes doesn't work */
		if(pthread_setname_np(inc_x_thread, "child-p")) {
			fprintf(stderr, "%p: Error setting thread name (%d)\n", inc_x_thread, errno);
			return 1;
		}
#endif

		/* increment y to 100 in the first thread */
		while(++y < 100);
		//sleep(1);
		printf("y increment finished\n");

		if (argc > 2) {
			if(pthread_cancel(inc_x_thread)) {
				fprintf(stderr, "Error canceling thread\n");
				return 3;
			}
		}

		/* wait for the second thread to finish */
		if(pthread_join(inc_x_thread, NULL)) {

			fprintf(stderr, "Error joining thread\n");
			return 2;

		}

		/* show the results - x is now 100 thanks to the second thread */
		printf("x: %d, y: %d\n", x, y);
		i++;
	}

	return 0;

}

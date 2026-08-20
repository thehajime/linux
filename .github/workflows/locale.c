#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
	int ret = getpid();
	printf("strerr = %s\n", strerror(errno));
	return 0;
}

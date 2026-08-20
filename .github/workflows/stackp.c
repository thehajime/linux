#include <unistd.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <errno.h>


int main(int argc, char *argv[])
{
	char str[64];

	write(2, "Done\nChild\n", 11);
}

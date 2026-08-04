SRCS := clone.c futex.c nullptr.c ctest.c stackp.c do_getpid.c init_array.c entropy.c locale.c vdso_test.c pthread.c
BKUP := exit.c noop.c
TARGETS := $(SRCS:.c=)

MASTER_CC ?= docker run -it --rm -v /dev/shm:/dev/shm -v /home:/home -v /tmp:/tmp --user tazaki -w `pwd` alpine-local:3.20.3 cc
CC=${MASTER_CC}

CFLAGS_exit = -nostdlib -pie -fPIE -fomit-frame-pointer
CFLAGS += -g -O0 -static-pie
all: ${TARGETS}

tests: ${TARGETS}
	set +x
	./clone host
	./futex
	./nullptr 1 0 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "139" ] ; then false ; fi
	./nullptr 1 1 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "11" -a "$$RET" -ne "139" ] ; then false ; fi
	./nullptr 0 0 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "139" ] ; then false ; fi
	./nullptr 0 1 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "11" -a "$$RET" -ne "139" ] ; then false ; fi
	./nullptr 2 0 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "136" ] ; then false ; fi
	./nullptr 2 1 || RET=$$? ; echo "code="$$RET && if [ "$$RET" -ne "11" -a "$$RET" -ne "136" ] ; then false ; fi
	./nullptr
	echo "test" | ./ctest
	./stackp
	./entropy 10000
	./locale
	./vdso_test -c 10000
	./pthread
	./pthread 10
	./pthread 10 1
	set -x

clean:
	rm -f ${TARGETS}

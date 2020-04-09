# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
# also do not print "Entering directory..." messages from make
.SUFFIXES:
MAKEFLAGS += -r --no-print-directory

KCONFIG?=defconfig

ifneq ($(silent),1)
  ifneq ($(V),1)
	QUIET_AUTOCONF       = @echo '  AUTOCONF '$@;
	Q = @
  endif
endif

PREFIX   := /usr

ifeq (,$(srctree))
  srctree := $(patsubst %/,%,$(dir $(shell pwd)))
  srctree := $(patsubst %/,%,$(dir $(srctree)))
endif
export srctree

-include ../scripts/Makefile.include

# OUTPUT fixup should be *after* include ../scripts/Makefile.include
ifneq ($(OUTPUT),)
  OUTPUT := $(OUTPUT)/tools/lkl/
else
  OUTPUT := $(CURDIR)/
endif
export OUTPUT


all:

export CFLAGS += -I$(OUTPUT)/include -Iinclude -Wall -g -O2 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

-include Targets

TARGETS := $(progs-y:%=$(OUTPUT)%)
TARGETS += $(libs-y:%=$(OUTPUT)%)
all: $(TARGETS)

# rule to build linux.o
$(OUTPUT)lib/linux.o:
	$(Q)CFLAGS= $(MAKE) -C ../.. ARCH=um $(KOPT) $(KCONFIG)
	$(Q)CFLAGS= $(MAKE) -C ../.. ARCH=um $(KOPT) install INSTALL_PATH=$(OUTPUT)

$(OUTPUT)liblinux.a: $(OUTPUT)lib/linux.o
	$(QUIET_AR)$(AR) -rc $@ $^

# rule to link programs
$(OUTPUT)%: $(OUTPUT)liblinux.a
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$*-y)

clean:
	$(call QUIET_CLEAN, objects)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd'\
	 -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, liblinux.a)$(RM) $(OUTPUT)/liblinux.a

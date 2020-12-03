# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
# also do not print "Entering directory..." messages from make
.SUFFIXES:
MAKEFLAGS += -r --no-print-directory

ifneq ($(silent),1)
  ifneq ($(V),1)
	QUIET_AUTOCONF       = @echo '  AUTOCONF '$@;
	Q = @
  endif
endif

PREFIX   := /usr

srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
export srctree

-include ../scripts/Makefile.include

# OUTPUT fixup should be *after* include ../scripts/Makefile.include
ifneq ($(OUTPUT),)
  OUTPUT := $(OUTPUT)/tools/um/
else
  OUTPUT := $(CURDIR)/
endif
export OUTPUT
export objtree := $(OUTPUT)/../..

export CFLAGS += -I$(OUTPUT)/include -Iinclude -Wall -g -O2 -Wextra -fPIC \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

-include Targets

TARGETS := $(progs-y:%=$(OUTPUT)%)
TARGETS += $(libs-y:%=$(OUTPUT)%)
all: $(TARGETS)

# rule to build linux.o
$(objtree)/linux.o:
	$(Q)echo ""
	$(Q)echo ""
	$(Q)echo "==> $@ isn't found; please make ARCH=um"
	$(Q)echo ""
	$(Q)echo ""
	$(Q)exit 1

$(OUTPUT)lib/linux.o: $(objtree)/linux.o
	$(Q)cp $(objtree)/linux.o $(OUTPUT)lib/linux.o

$(OUTPUT)liblinux.a: $(OUTPUT)lib/linux.o $(OUTPUT)uml/liblinux-in.o
	$(QUIET_AR)$(AR) -rc $@ $^

# rule to link programs
$(OUTPUT)%: $(OUTPUT)%-in.o $(OUTPUT)liblinux.a
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$(notdir $*)-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$(notdir $*)-y)

# rule to build objects
$(OUTPUT)%-in.o: FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(patsubst %/,%,$(dir $*)) obj=$(notdir $*)

RM := rm -f
clean:
	$(call QUIET_CLEAN, objects)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd'\
	 -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, liblinux.a)$(RM) $(OUTPUT)/liblinux.a
	$(call QUIET_CLEAN, $(TARGETS))$(RM) $(TARGETS)

FORCE: ;
.PHONY: all clean FORCE
.IGNORE: clean
.NOTPARALLEL : lib/linux.o

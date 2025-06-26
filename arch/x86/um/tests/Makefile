include $(srctree)/init/Makefile.nolibc

ccflags-y := -I$(obj)

um-tests-y += registers.o

userprogs += test-fp-save-restore
test-fp-save-restore-userccflags := -static $(NOLIBC_USERCFLAGS) -msse

obj-$(CONFIG_KUNIT_UAPI) += um-tests.o

$(obj)/registers.o: $(obj)/test-fp-save-restore

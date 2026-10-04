include $(srctree)/init/Makefile.nolibc

ccflags-y := -I$(obj)

um-tests-y += registers.o

userprogs += test-fp-save-restore
test-fp-save-restore-userccflags := $(NOLIBC_USERCFLAGS) -msse
#test-fp-save-restore-userccflags += -fplugin=$(abspath $(objtree))/scripts/gcc-plugins/swmmu_plugin.so
#test-fp-save-restore-userccflags += -fplugin-arg-swmmu_plugin-all-access=1

obj-$(CONFIG_KUNIT_UAPI) += um-tests.o

$(obj)/registers.o: $(obj)/test-fp-save-restore

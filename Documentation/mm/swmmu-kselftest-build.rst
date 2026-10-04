=============================
Building SWMMU NOMMU kselftests
=============================

This describes a local build of the SWMMU NOMMU selftests using a separate
kernel output directory. It assumes the kernel tree has already been
configured for UML NOMMU SWMMU and that the host has GCC plugin headers for
the compiler used to build the plugin.

Build the GCC plugin with the host compiler, then build and install the
kselftests with the same plugin and the all-access instrumentation option::

  make -C tools/testing/selftests/mm/nommu \
       SWMMU_BUILD_PLUGIN=1 SWMMU_HOSTCXX=g++ \
       SWMMU_PLUGIN="$PWD/build-kselftest/swmmu_plugin.so" \
       swmmu-plugin

  make ARCH=um NOMMU=1 O=build-kselftest \
       SWMMU_ALL_ACCESS=1 \
       SWMMU_PLUGIN="$PWD/build-kselftest/swmmu_plugin.so" \
       TARGETS=mm/nommu kselftest-all kselftest-install

The plugin output path is explicit so the plugin can be reused by the
selftest build. `SWMMU_ALL_ACCESS=1` is passed to the NOMMU selftest Makefile,
which applies its plugin option to `nommu_swmmu_test`.

The installed tests are placed under
`build-kselftest/kselftest/kselftest_install`. To stage them into a test
rootfs, copy that directory's contents into the rootfs location expected by
its init script. Avoid `rsync --delete` against a populated rootfs directory,
since it removes files that are not part of the kselftest install tree.

For a clean rebuild, remove only the dedicated output directory before
repeating the kernel selftest build::

  rm -rf build-kselftest

The plugin can also be rebuilt independently by rerunning the first command;
its prerequisites include the plugin source and GCC plugin headers.

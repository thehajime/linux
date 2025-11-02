#!/bin/bash

SCRIPT_DIR="$(cd $(dirname "${BASH_SOURCE[0]}"); pwd)"

# macos has different syntax
OUTPUT=$SCRIPT_DIR/$(date "+%Y-%m-%d")
mkdir -p $OUTPUT

TRIALS="1"
ENTRIES="1000000"
VSIZES="1 8 256 1024 8192"
RUNTIMES="build/mmu build"


echo "$(tput bold)== um (mmu) ($test-$num-$vsize)  ==$(tput sgr0)"
../linux-um-nommu/build-mmu/vmlinux ubd0=./alpine-test.ext3 rw mem=1024m loglevel=0 init=/bench.sh \
  | tee "$OUTPUT/um-mmu.dat"

echo "$(tput bold)== um (nommu) ($test-$num-$vsize)  ==$(tput sgr0)"
../linux-um-nommu/build/vmlinux ubd0=./alpine-test.ext3 rw mem=1024m loglevel=0 init=/bench.sh \
  | tee "$OUTPUT/um-nommu.dat"

echo "$(tput bold)== host (mmu) ($test-$num-$vsize)  ==$(tput sgr0)"
sh docker/alpine/lmbench_run.sh \
  |& tee "$OUTPUT/native.dat"
./zpoline-bench/do_getpid -c 100 | tee -a "$OUTPUT/native.dat"

bash ${SCRIPT_DIR}/um-nommu-plot.sh ${OUTPUT}

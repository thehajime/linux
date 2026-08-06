#!/bin/bash

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"

OUTPUT="$1"
DIRS="read fill"

mkdir -p "$OUTPUT/out/"

# parse outputs

for f in `ls $OUTPUT/*.dat |grep -v iperf3 | grep -v ltp`
do
   cat $f | grep -a microsec | sed "s/.*:\(.*\)/\1/" | awk '{print $1}' \
	> $OUTPUT/out/`basename $f .dat`-lmbench-out.dat

   cat $f | grep -a average |grep -v time | awk '{print $2 $3}' \
	> $OUTPUT/out/`basename $f .dat`-getpid-out.dat
done

# parse iperf3 result
for f in `ls $OUTPUT/*-iperf3.dat`
do
    cat $f | grep receiver | awk '{print $7}' \
	> $OUTPUT/out/`basename $f .dat`-out.dat
done

gnuplot  << EndGNUPLOT
set terminal postscript eps lw 3 "Helvetica" 24
set output "${OUTPUT}/out/lmbench.eps"
#set xtics font "Helvetica,14"
set pointsize 2
set xzeroaxis
set grid ytics

set boxwidth 0.2
set style fill pattern

set size 1.0,0.8
set key top left

set xrange [-0.5:10]
set xtics ('select-10' 0, 'select-100' 1, 'select-1000' 2, 'syscall' 3, 'read' 4, 'write' 5, 'stat' 6, 'open/close' 7, 'fork+sh' 8, 'fork+execve' 9)
set xtics rotate by 45 right
set yrange [0.01:100000]
set ylabel "Latency (usec)"
set logscale y

plot \
   '${OUTPUT}/out/native-lmbench-out.dat' usin (\$0-0.4):(\$1) w boxes fill patter 2 lt 1 lc rgb "red" title "native", \
   '${OUTPUT}/out/um-mmu-lmbench-out.dat' usin (\$0-0.2):(\$1) w boxes fill patter 2 lt 1 lc rgb "green" title "um(mmu)" ,\
   '${OUTPUT}/out/um-mmu-seccomp-lmbench-out.dat' usin (\$0-0):(\$1) w boxes fill patter 2 lt 1 lc rgb "dark-green" title "um(mmu(s))" ,\
   '${OUTPUT}/out/um-nommu-lmbench-out.dat' usin (\$0+0.2):(\$1) w boxes fill patter 2 lt 1 lc rgb "royalblue" title "um(nommu)" ,\
   '${OUTPUT}/out/um-nommu-seccomp-lmbench-out.dat' usin (\$0+0.4):(\$1) w boxes fill patter 2 lt 1 lc rgb "blue" title "um(nommu(s))"

set terminal png lw 3 14 crop
set output "${OUTPUT}/out/lmbench.png"
replot

EndGNUPLOT

echo -e "### lmbench (usec)\n"
echo -e "|select-10\n|select-100\n|select-1000\n|syscall\n|read\n|write\n|stat\n|open/close\n|fork+sh\n|fork+execve" > /tmp/a

echo -e "||native|um|um-mmu(s)|um-nommu|um-nommu(sas-s)|um-nommu(sas-z)|um-nommu(s)|\n|--|--|--|--|--|--|--|--|"; paste -d "|" `ls ${OUTPUT}/out/*-lmbench-out.dat` | sed "s/\(.*\)/\|\1\|/" | paste /tmp/a - | column -t

rm -f /tmp/a

echo ""
echo -e "### do_getpid bench (nsec)\n"
for f in `ls $OUTPUT/*.dat |grep -v iperf3`
do
 export $(basename $f .dat|sed "s/-/_/g")=`grep -a aver $f | grep -v time | awk '{print $2}'`
done
echo -e "||native|um|um-mmu(s)|um-nommu|um-nommu(s)|um-nommu(sas-s)|um-nommu(sas-z)|\n|--|--|--|--|--|--|--|--|"
echo "|getpid | ${native} | ${um_mmu} | ${um_mmu_seccomp} | ${um_nommu}| ${um_nommu_seccomp}| ${um_nommu_sas_seccomp}|${um_nommu_sas_zpoline}|"


# iperf result

gnuplot  << EndGNUPLOT
set terminal postscript eps lw 3 "Helvetica" 24
set output "${OUTPUT}/out/iperf3.eps"
#set xtics font "Helvetica,14"
set pointsize 2
set xzeroaxis

set boxwidth 0.2
set style fill pattern

set size 1.0,0.8
set key top left

set xrange [-0.5:]
set xtics ('iperf(f)' 0, 'iperf(r)' 1)
#set xtics rotate by 45 right
set yrange [:50]
set ytics 10
set ylabel "Goodput (Gbps)"

plot \
   '${OUTPUT}/out/native-iperf3-out.dat' usin (\$0-0.4):(\$1/1000) w boxes fill patter 2 lt 1 lc rgb "red" title "native",\
   '${OUTPUT}/out/um-mmu-iperf3-out.dat' usin (\$0-0.2):(\$1/1000) w boxes fill patter 2 lt 1 lc rgb "green" title "um(mmu)" ,\
   '${OUTPUT}/out/um-mmu-seccomp-iperf3-out.dat' usin (\$0-0):(\$1/1000) w boxes fill patter 2 lt 1 lc rgb "dark-green" title "um(mmu(s))" ,\
   '${OUTPUT}/out/um-nommu-iperf3-out.dat' usin (\$0+0.2):(\$1/1000) w boxes fill patter 2 lt 1 lc rgb "royalblue" title "um(nommu)" ,\
   '${OUTPUT}/out/um-nommu-seccomp-iperf3-out.dat' usin (\$0+0.4):(\$1/1000) w boxes fill patter 2 lt 1 lc rgb "blue" title "um(nommu(s))"


set terminal png lw 3 14 crop
set output "${OUTPUT}/out/iperf3.png"
replot

EndGNUPLOT

echo ""
echo -e "### iperf3 bench (Mbps)\n"
for f in `ls $OUTPUT/out/*iperf3*.dat`
do
 export $(basename $f .dat|sed "s/-/_/g" | sed "s/_iperf3_out/_f/")=`cat $f|awk NR==1`
 export $(basename $f .dat|sed "s/-/_/g" | sed "s/_iperf3_out/_r/")=`cat $f|awk NR==2`


done
echo -e "||native|um|um-mmu(s)|um-nommu|um-nommu(s)|um-nommu(sas-s)|um-nommu(sas-z)|\n|--|--|--|--|--|--|--|--|"
echo "|iperf3(f) | ${native_f} | ${um_mmu_f} | ${um_mmu_seccomp_f} | ${um_nommu_f}| ${um_nommu_seccomp_f}|${um_nommu_sas_seccomp_f}|${um_nommu_sas_zpoline_f}|"
echo "|iperf3(r) | ${native_r} | ${um_mmu_r} | ${um_mmu_seccomp_r} | ${um_nommu_r}| ${um_nommu_seccomp_r}|${um_nommu_sas_seccomp_r}|${um_nommu_sas_zpoline_r}|"

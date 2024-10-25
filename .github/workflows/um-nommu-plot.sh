#!/bin/bash

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"

OUTPUT="$1"
DIRS="read fill"

mkdir -p "$OUTPUT/out/"

# parse outputs

for f in `ls $OUTPUT/*.dat`
do
   cat $f | grep microsec | sed "s/.*:\(.*\)/\1/" | awk '{print $1}' \
	> $OUTPUT/out/`basename $f .dat`-out.dat

   cat $f | grep average | awk '{print $2 $3}' \
	> $OUTPUT/out/`basename $f .dat`-getpid-out.dat
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
   '${OUTPUT}/out/um-mmu-out.dat' usin (\$0-0.2):(\$1) w boxes fill patter 2 lt 1 lc rgb "green" title "um(mmu)" ,\
   '${OUTPUT}/out/um-nommu-out.dat' usin (\$0):(\$1) w boxes fill patter 2 lt 1 lc rgb "blue" title "um(nommu)" ,\
   '${OUTPUT}/out/native-out.dat' usin (\$0+0.2):(\$1) w boxes fill patter 2 lt 1 lc rgb "red" title "native" 

set terminal png lw 3 14 crop
set output "${OUTPUT}/out/lmbench.png"
replot

EndGNUPLOT

echo -e "### lmbench (usec)\n"
echo -e "|select-10\n|select-100\n|select-1000\n|syscall\n|read\n|write\n|stat\n|open/close\n|fork+sh\n|fork+execve" > /tmp/a

echo -e "||native|um|um-nommu|\n|--|--|--|--|"; paste -d "|" `ls ${OUTPUT}/out/*.dat |grep -v getpid` | sed "s/\(.*\)/\|\1\|/" | paste /tmp/a - | column -t

rm -f /tmp/a

echo ""
echo -e "### do_getpid bench (nsec)\n"
for f in `ls $OUTPUT/*.dat`
do
 export $(basename $f .dat|sed "s/-/_/")=`grep aver $f | awk '{print $2}'`
done
echo -e "||native|um|um-nommu|\n|--|--|--|--|"
echo "|getpid | ${native} | ${um_mmu} | ${um_nommu}|"

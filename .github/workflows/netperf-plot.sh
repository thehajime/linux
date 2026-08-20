#!/bin/bash

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
OUTPUT="$1"

mkdir -p "$OUTPUT/out/"

# parse outputs
# TCP_STREAM
for f in `ls $OUTPUT/*-iperf3.dat`
do
    cat $f | grep bits |grep STREAM | awk -F',' '{print $3" " $1}' \
	> $OUTPUT/out/`basename $f .dat|sed "s/iperf3/netperf/g"`-fwd-out.dat
    cat $f | grep bits |grep MAERTS | awk -F',' '{print $3" " $1}' \
	> $OUTPUT/out/`basename $f .dat|sed "s/iperf3/netperf/g"`-rev-out.dat
done # end of ${DIR}


PSIZE_XTICS="('64' 0, '128' 2, '256' 4, '512' 6, '1024' 8, '1500' 10, '65507' 12)"
PAT_NATIVE='fill patter 2 lc rgb "red"'
PAT_MMU='fill patter 2 lc rgb "green"'
PAT_MMU_S='fill patter 2 lc rgb "dark-green"'
PAT_NOMMU_S='fill patter 2 lc rgb "royalblue"'
PAT_NOMMU_Z='fill patter 2 lc rgb "blue"'

gnuplot  << EndGNUPLOT
set terminal postscript color eps lw 3 "Helvetica" 24
set output "${OUTPUT}/out/tcp-stream.eps"
#set xtics font "Helvetica,14"
set pointsize 2
set xzeroaxis
set grid ytics

set boxwidth 0.3
set style fill pattern

set size 1.0,0.9
set key font ",18"
set key top left Left reverse
#set key above vertical maxrows 2

set xrange [-1:13]
set xtics ${PSIZE_XTICS}
set xlabel "Payload size (bytes)"
set yrange [-50:40]
#set ytics ('0' -10, '5' -5, '0' 0, '5' 5, '10' 10)
set ylabel "Goodput (Gbps)" offset +0.8



plot \
   '${OUTPUT}/out/native-netperf-fwd-out.dat' usin (\$0*2-0.6):(\$2/1000) w boxes $PAT_NATIVE title "native" ,\
   '${OUTPUT}/out/um-mmu-netperf-fwd-out.dat' usin (\$0*2-0.3):(\$2/1000) w boxes $PAT_MMU title "um(mmu)" ,\
   '${OUTPUT}/out/um-mmu-seccomp-netperf-fwd-out.dat' usin (\$0*2-0):(\$2/1000) w boxes $PAT_MMU_S title "um(mmu(s))" ,\
   '${OUTPUT}/out/um-nommu-netperf-fwd-out.dat' usin (\$0*2+0.3):(\$2/1000) w boxes $PAT_NOMMU_S title "um(nommu(s))" ,\
   '${OUTPUT}/out/um-nommu-seccomp-netperf-fwd-out.dat' usin (\$0*2+0.6):(\$2/1000) w boxes $PAT_NOMMU_Z title "um(nommu(z))" ,\
   '${OUTPUT}/out/native-netperf-rev-out.dat' usin (\$0*2-0.6):(\$2*-1/1000) w boxes $PAT_NATIVE notitle ,\
   '${OUTPUT}/out/um-mmu-netperf-rev-out.dat' usin (\$0*2-0.3):(\$2*-1/1000) w boxes $PAT_MMU notitle ,\
   '${OUTPUT}/out/um-mmu-seccomp-netperf-rev-out.dat' usin (\$0*2-0):(\$2*-1/1000) w boxes $PAT_MMU_S notitle ,\
   '${OUTPUT}/out/um-nommu-netperf-rev-out.dat' usin (\$0*2+0.3):(\$2*-1/1000) w boxes $PAT_NOMMU_S notitle ,\
   '${OUTPUT}/out/um-nommu-seccomp-netperf-rev-out.dat' usin (\$0*2+0.6):(\$2*-1/1000) w boxes $PAT_NOMMU_Z notitle


set terminal png lw 3 14 crop
set key font ",12"
set size 1.0,1.0
set ylabel "Goodput (Gbps)" offset +0.5
set output "${OUTPUT}/out/tcp-stream.png"
replot


#set terminal dumb
#unset key
#unset output
#replot

quit
EndGNUPLOT



echo ""
echo -e "### netperf bench (TCP_STREAM) (Mbps)\n"
echo -e "| psize | native | um |um-mmu(s) | um-nommu | um-nommu(s) | um-nommu(sas-s) | um-nommu(sas-z)|\n|--|--|--|--|--|--|--|--|"
join  $OUTPUT/out/native-netperf-fwd-out.dat  $OUTPUT/out/um-mmu-netperf-fwd-out.dat \
    | join -  $OUTPUT/out/um-mmu-seccomp-netperf-fwd-out.dat \
    | join - $OUTPUT/out/um-nommu-netperf-fwd-out.dat \
    | join - $OUTPUT/out/um-nommu-seccomp-netperf-fwd-out.dat \
    | join - $OUTPUT/out/um-nommu-sas-seccomp-netperf-fwd-out.dat \
    | join - $OUTPUT/out/um-nommu-sas-zpoline-netperf-fwd-out.dat \
    | sed "s/ /\|/g" |  sed "s/^/\|/" | sed "s/$/\|/"

echo ""
echo -e "### netperf bench (TCP_MAERTS) (Mbps)"
echo -e "| psize | native | um |um-mmu(s) | um-nommu | um-nommu(s) | um-nommu(sas-s) | um-nommu(sas-z)|\n|--|--|--|--|--|--|--|--|"
join  $OUTPUT/out/native-netperf-rev-out.dat  $OUTPUT/out/um-mmu-netperf-rev-out.dat \
    | join -  $OUTPUT/out/um-mmu-seccomp-netperf-rev-out.dat \
    | join - $OUTPUT/out/um-nommu-netperf-rev-out.dat \
    | join - $OUTPUT/out/um-nommu-seccomp-netperf-rev-out.dat \
    | join - $OUTPUT/out/um-nommu-sas-seccomp-netperf-rev-out.dat \
    | join - $OUTPUT/out/um-nommu-sas-zpoline-netperf-rev-out.dat \
    | sed "s/ /\|/g" |  sed "s/^/\|/" | sed "s/$/\|/"

#!/bin/sh
# SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
# SPDX-License-Identifier: MIT

WSDFILE=wsd_scale_data.txt
DISTFILE=dist_scale_data.txt
FIFOFILE=fifo_scale_data.txt
SHARDEDFIFOFILE=sharded_fifo_scale_data.txt
rm -f $WSDFILE $DISTFILE $FIFOFILE $SHARDEDFIFOFILE

for THREADS in `seq 1 4`
do
    WORK=0
    while [ $WORK -lt 250 ]
    do
        echo "$THREADS threads with $WORK work"
        ./bin/test_wsd_scale $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, ($2*$4)/$8}' >> $WSDFILE
        ./bin/test_dist_fifo $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, ($2*$4)/$8}' >> $DISTFILE
        ./bin/test_fifo_steal_scale $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, ($2*$4)/$8}' >> $FIFOFILE
        ./bin/test_sharded_fifo_steal_scale $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, ($2*$4)/$8}' >> $SHARDEDFIFOFILE
        WORK=`expr $WORK + 50`
    done
    echo >> $WSDFILE
    echo >> $DISTFILE
    echo >> $FIFOFILE
    echo >> $SHARDEDFIFOFILE
done

echo "set term png size 1024,768; set ticslevel 0; set xlabel 'Threads'; set ylabel 'Work Factor'; set zlabel 'Events Per Second'; splot 'wsd_scale_data.txt' with lines, 'dist_scale_data.txt' with lines, 'fifo_scale_data.txt' with lines, 'sharded_fifo_scale_data.txt' with lines" | gnuplot > steal_perf.png

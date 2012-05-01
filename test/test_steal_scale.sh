#!/bin/sh

WSDFILE=wsd_scale_data.txt
DISTFILE=dist_scale_data.txt
rm -f $WSDFILE $DISTFILE

for THREADS in `seq 1 8`
do
    WORK=0
    while [ $WORK -lt 250 ]
    do
        echo "$THREADS threads with $WORK work"
        ./bin/test_wsd_scale $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, $4/$8}' >> $WSDFILE
        ./bin/test_dist_fifo $THREADS 10000000 $WORK | awk '/timing/{print $2, $6, $4/$8}' >> $DISTFILE
        WORK=`expr $WORK + 50`
    done
    echo >> $WSDFILE
    echo >> $DISTFILE
done

echo "set ticslevel 0; set term png size 1024,768; set xlabel 'Threads'; set ylabel 'Work Factor'; set zlabel 'Events Per Second';set zlabel 'Ops Per Second'; splot './wsd_scale_data.txt' with lines, './dist_scale_data.txt' with lines" | gnuplot > steal_perf.png

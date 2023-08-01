#!/bin/sh
# SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
# SPDX-License-Identifier: MIT

THREADS=$1
OPS=$2
if [ "$THREADS" == "" ]
then
    THREADS=4
fi
if [ "$OPS" == "" ]
then
    OPS=10000000
fi

for cur in `seq 1 $THREADS`
do
    ./bin/test_cpu_scale $cur $OPS
done | awk '
/total/ {
    testName = $2;
    if(!data[testName]) {
        data[testName] = testName;
    }
    data[testName] = data[testName] " " $8;
    if(!firstTest) {
        firstTest = testName;
    }
    if(testName == firstTest) {
        ++testCount;
    }
}

END {
    cur = 0;
    printf("threads");
    while(cur < testCount) {
        printf(" %s", cur + 1);
        ++cur;
    }
    print "";
    for(cur in data) {
        print data[cur];
    }
}
'

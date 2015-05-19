#!/bin/sh
# Copyright (c) 2012-2015, Brian Watling and other contributors
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

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

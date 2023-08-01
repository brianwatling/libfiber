// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

package main

import (
	"fmt"
	"time"
)

func receiver(ch chan int) {
    last := time.Now()
    for i := 0; i < 100000000; i++ {
        n := <- ch
        if (n % 10000000) == 0 {
            now := time.Now()
            fmt.Printf("Received 10000000 in %g seconds\n", 0.000000001 * float64(now.Sub(last).Nanoseconds()))
            last = now
        }
    }
}

func sender(ch chan int) {
    n := 1
    for i := 0; i < 100000000; i++ {
        ch <- n
        n += 1
    } 
}

func main() {
    ch1 := make(chan int, 1000)
    ch2 := make(chan int, 1000)
    go receiver(ch1)
    go receiver(ch2)
    go sender(ch2)
    sender(ch1)
}

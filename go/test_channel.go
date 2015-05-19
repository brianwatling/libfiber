// Copyright (c) 2012-2015, Brian Watling and other contributors
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

package main

import "fmt"
import "time"

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

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
import "flag"

var send_count *int

func receiver(ch chan int, id int, done chan int) {
    last := time.Now()
    for i := 0; i < *send_count; i++ {
        n := <- ch
        if (n % 10000000) == 0 {
            now := time.Now()
            fmt.Printf("%v Received 10000000 in %g seconds\n", id, 0.000000001 * float64(now.Sub(last).Nanoseconds()))
            last = now
        }
    }
    done <- 1
}

func sender(ch chan int) {
    n := 1
    for i := 0; i < *send_count; i++ {
        ch <- n
        n += 1
    } 
}

func main() {
    count := flag.Int("count", 2, "number of senders/receivers")
    send_count = flag.Int("send_count", 100000000, "number of messages sent by each sender")
    flag.Parse()
    ch1 := make(chan int, 1000)
    done := make(chan int)
    for i := 0; i < *count; i++ {
        go receiver(ch1, i, done)
        go sender(ch1)
    }
    for i := 0; i < *count; i++ {
        <- done
    }
}

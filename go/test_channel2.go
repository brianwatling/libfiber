package main

import "fmt"
import "time"
import "flag"

func receiver(ch chan int, done chan int) {
    last := time.Now()
    for i := 0; i < 100000000; i++ {
        n := <- ch
        if (n % 10000000) == 0 {
            now := time.Now()
            fmt.Printf("Received 10000000 in %g seconds\n", 0.000000001 * float64(now.Sub(last).Nanoseconds()))
            last = now
        }
    }
    done <- 1
}

func sender(ch chan int) {
    n := 1
    for i := 0; i < 100000000; i++ {
        ch <- n
        n += 1
    } 
}

func main() {
    count := flag.Int("count", 2, "number of senders/receivers")
    flag.Parse()
    ch1 := make(chan int, 1000)
    done := make(chan int)
    for i := 0; i < *count; i++ {
        go receiver(ch1, done)
        go sender(ch1)
    }
    for i := 0; i < *count; i++ {
        <- done
    }
}

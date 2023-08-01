<!--
SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
SPDX-License-Identifier: CC0-1.0
-->

[![REUSE compliant](https://api.reuse.software/badge/github.com/brianwatling/libfiber)](https://api.reuse.software/info/github.com/brianwatling/libfiber)

# A User Space Threading Library Supporting Multi-Core Systems

- Lightweight user threads with support for blocking IO and fast context switching (ie. similar to Erlang or Go but using C)
- Native system calls are automatically shimmed / intercepted and converted to be fiber-compatible
- Fast, scalable load balancing across multiple cores
- Lock-free data structures
- Supports x86 and x86_64. Further architectures can be added easily.
- Supports native event backends on Linux and Solaris
- Supports libev event backend

## Motivation

- Why Events Are A Bad Idea (for high-concurrency servers) - Rob von Behren, Jeremy Condit, and Eric Brewer
    - Specifically, the following quote summarizes nicely:
        "...the duality argument of Lauer and Needham ... implies that good implementations of thread systems and event systems will have similar performance."


## Building

- Type 'make' to build the library and run the unit tests
- Link your application to libfiber.so
- libfiber.so overrides many system calls. Be careful to link libfiber in the correct order (the io shims will either work or they won't!)
- The build system will attempt to detect and use gcc split stack support (Golang uses this for their stacks). 

## Dependencies

None for the native event engine. If using the libev event engine then libev is required:

```bash
sudo apt-get install libev-dev
```

## Example

- See example/echo_server.c for an example.
- The basic idea is that you write blocking code and libfiber makes it event driven for you.

Spawn a fiber running 'client_function' per client:

    ...
    while((sock = accept(server_socket, NULL, NULL)) >= 0) {
        fiber_t* client_fiber = fiber_create(10240, &client_function, (void*)(intptr_t)sock);
        fiber_detach(client_fiber);
    }
    ...

'client_function' does a blocking read() and write() on the socket:

    void* client_function(void* param)
    {
        ...
        while((num_read = read(sock, buffer, sizeof(buffer))) > 0) {
            if(num_read != write(sock, buffer, num_read)) {
                break;
            }
        }
        ...
    }

## Usage

- Call fiber_manager_init() at the beginning of your program
    - Specify the number of kernel threads (ie. CPUs) to use
    - This will initialize the event system and shim blocking IO calls
    - Call fiber_shutdown() at exit if you'd like to clean up.
    - TODO(bwatling): test fiber_manager_init after having called fiber_shutdown
- Familiar threading concepts are available in include/
    - Mutexes
    - Semaphores
    - Read/Write Mutexes
    - Barriers
    - Spin Locks
    - Condition Variables
- Lock free data structures available in include/
    - Single-Producer/Single-Consumer FIFO
    - Multi-Producer/Single-Consumer FIFO (two implementations with different properties)
    - Multi-Producer/Multi-Consumer LIFO
    - Multi-Producer/Multi-Consumer FIFO (using hazard pointers)
    - Fixed-Size Ring Buffer
    - Work Stealing

## Performance

- Anecdotally:
    - libfiber's mutex objects significantly outperform pthread's mutex objects under contention. This is because a contended mutex requires context switches.
    - libfiber's channels signifcantly outperform Go's channels. Both libfiber's  and Go's channels use a mutex internally - libfiber's fast mutex gives an advantage.
        - See go/test_channel.go, go/test_channel2.go, test/test_bounded_mpmc_channel.c, and test/test_bounded_mpmc_channel2.c


- TODO: automated benchmarks with real numbers

## Testing

- Thorough unit tests
- Over 90% test coverage ('make coveragereport', then see bin/lcov/index.html)
    - Only extreme failure cases missing, such as NOMEM
- Tested on x86 Linux, x86_64 Linux, x86 Solaris 10
- Separate tests for lock free data structure and hazard pointers

## TODO

- Detect architecture automatically using known defines

## License

MIT License

Copyright (c) 2012-2023 Brian Watling and other contributors

## Contributors

- Brian Watling (https://github.com/brianwatling)
- plouj (https://github.com/plouj)

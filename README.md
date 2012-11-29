# A User Space Threading Library Supporting Multi-Core Systems

- Lightweight user threads with support for blocking IO and fast context switching (ie. similar to Erlang or Go but using C)
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
- Be sure to define the architecture when including libfiber's headers. You can specify the following gcc flags:
    - For x86 64 bit: -m64 -DARCH_x86_64
    - For x86 32 bit: -m32 -march=i686 -DARCH_x86
- libfiber.so overrides many system calls so make sure you know what you're doing
- The makefile will attempt to detect gcc split stack support (Go uses this). This requires gcc 4.7 or higher. I recommend using this.
    - make CC=gcc-4.7

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

## Contributors

- Brian Watling (https://github.com/brianwatling)
- plouj (https://github.com/plouj)

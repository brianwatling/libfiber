# A User Space Threading Library Supporting Multi-Core Systems

- Lightweight user threads with support for blocking IO and fast context switching (ie. similar to Erlang or Go but using C)
- Fast, scalable load balancing across multiple cores
- Lock-free data structures
- Supports x86 and x86_64. Further architectures can be added easily.
- Supports native event backends on Linux and Solaris
- Supports libev event backend

## Building

- Type 'make' to build the library and run the unit tests
- Link your application to libfiber.so
- Be sure to define the architecture when including libfiber's headers
- libfiber.so overrides many system calls so make sure you know what you're doing
- Set USE_NATIVE_EVENTS=no to use libev for events (slower due to mutexes)

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

- TODO: real benchmarks

## Testing

- Thorough unit tests
- Over 90% test coverage ('make coveragereport', then see bin/lcov/index.html)
    - Only extreme failure cases missing, such as NOMEM
- Tested on x86 Linux, x86_64 Linux, x86 Solaris 10
- Separate tests for lock free data structure and hazard pointers

## Contributors

- Brian Watling (https://github.com/brianwatling)
- plouj (https://github.com/plouj)

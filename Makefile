
all: libfiber.so bin/echo_server runtests

VPATH += example src test submodules/libev

CFILES = \
    fiber_context.c \
    fiber_manager.c \
    fiber_mutex.c \
    fiber_semaphore.c \
    fiber_spinlock.c \
    fiber_cond.c \
    fiber.c \
    fiber_barrier.c \
    fiber_io.c \
    fiber_rwlock.c \
    hazard_pointer.c \
    work_stealing_deque.c \
    work_queue.c \
    fiber_scheduler_wsd.c \

USE_NATIVE_EVENTS ?= 1
ifeq ($(USE_NATIVE_EVENTS),1)
CFILES += fiber_event_native.c
else
CFILES += fiber_event_ev.c ev.c
endif

LDFLAGS += -lm

OS ?= $(shell uname -s)

#your compiler will pick the architecture by default
ARCH ?= $(shell uname -m)
ifeq ($(ARCH),i386)
ARCH=x86
endif
ifeq ($(ARCH),i86pc)
ARCH=x86
endif
ifeq ($(ARCH),i686)
ARCH=x86
endif

ifeq ($(ARCH),x86_64)
CFLAGS += -m64 -DARCH_x86_64
endif
ifeq ($(ARCH),x86)
CFLAGS += -m32 -march=i686 -DARCH_x86
endif

CFLAGS += -pthread -Wall -Iinclude -Isubmodules/libev -D_REENTRANT -ggdb -O3

#don't use split-stack on gcc 4.6 since it doesn't implement getcontext, setcontext, or makecontext
GCC46 = $(shell $(CC) -v 2>&1 | grep "gcc.*4.6" > /dev/null; echo $$?)
ifeq ($(GCC46),0)
SPLIT_STACK = 0
endif

#detect if CC supports -fsplit-stack. SPLIT_STACK should be '1' if it does
SPLIT_STACK ?= $(shell echo "int i;" | $(CC) -xc -fsplit-stack -c -o /dev/null - 2> /dev/null; if [ $$? -eq 0 ] ; then echo 1; else echo 0; fi)
ifeq ($(SPLIT_STACK),1)
STACK_STRATEGY ?= split
endif

STACK_STRATEGY ?= mmap

ifeq ($(STACK_STRATEGY),split)
CFLAGS += -fsplit-stack -DFIBER_STACK_SPLIT
endif
ifeq ($(STACK_STRATEGY),malloc)
CFLAGS += -DFIBER_STACK_MALLOC
endif
ifeq ($(STACK_STRATEGY),mmap)
CFLAGS += -DFIBER_STACK_MMAP
endif

USE_VALGRIND ?= 0
ifeq ($(USE_VALGRIND),1)
CFLAGS += -DUSE_VALGRIND
endif

ifeq ($(OS),Darwin)
USE_COMPILER_THREAD_LOCAL ?= 0
LDFLAGS += -read_only_relocs suppress
FAST_SWITCHING ?= 0
endif

ifeq ($(OS),SunOS)
CFLAGS += -DSOLARIS
LINKER_SHARED_FLAG ?= -G
LDFLAGSAFTER += -lrt -lsocket
endif

ifeq ($(OS),Linux)
CFLAGS += -DLINUX
LDFLAGSAFTER += -ldl -lm -lrt
endif

USE_COMPILER_THREAD_LOCAL ?= 1
LINKER_SHARED_FLAG ?= -shared
FAST_SWITCHING ?= 1
LDFLAGSAFTER ?= 

ifeq ($(USE_COMPILER_THREAD_LOCAL),1)
CFLAGS += -DUSE_COMPILER_THREAD_LOCAL
endif
ifeq ($(FAST_SWITCHING),1)
CFLAGS += -DFIBER_FAST_SWITCHING
endif

TESTS= \
    test_tryjoin \
    test_sleep \
    test_io \
    test_context \
    test_context_speed \
    test_basic \
    test_multithread \
    test_mpmc_stack \
    test_mpmc_fifo \
    test_spsc \
    test_mpsc \
    test_mpscr \
    test_wsd \
    test_mutex \
    test_semaphore \
    test_wait_in_queue \
    test_cond \
    test_barrier \
    test_spinlock \
    test_rwlock \
    test_hazard_pointers \
    test_lockfree_ring_buffer \
    test_lockfree_ring_buffer2 \
    test_unbounded_channel \
    test_channel_pingpong \
    test_unbounded_channel_pingpong \
    test_work_queue \
    test_yield_speed \
    test_dist_fifo \
    test_wsd_scale \
    test_multi_channel \
    test_bounded_mpmc_channel \
    test_bounded_mpmc_channel2 \
    test_fifo_steal_scale \
    test_sharded_fifo_steal_scale \

#    test_channel \
#    test_pthread_cond \

CC ?= /usr/bin/c99

OBJS = $(patsubst %.c,bin/%.o,$(CFILES))
PICOBJS = $(patsubst %.c,bin/%.pic.o,$(CFILES))
TESTBINARIES = $(patsubst %,bin/%,$(TESTS))
INCLUDES = $(wildcard include/*.h)
TESTINCLUDES = $(wildcard test/*.h)

libfiber.so: $(PICOBJS)
	$(CC) $(LINKER_SHARED_FLAG) $(LDFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGSAFTER)

bin/libfiber.so: libfiber.so
	cp $^ $@

tests: $(TESTBINARIES)

bin/echo_server: bin/echo_server.o bin/libfiber.so
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGSAFTER)

runtests: tests
	for cur in $(TESTS); do echo $$cur; LD_LIBRARY_PATH=..:$$LD_LIBRARY_PATH time ./bin/$$cur > /dev/null; if [ "$$?" -ne "0" ] ; then echo "ERROR $$cur - failed!"; fi; done

bin/test_%.o: test_%.c $(INCLUDES) $(TESTINCLUDES)
	$(CC) -Werror $(CFLAGS) -Isrc -c $< -o $@

bin/test_%: bin/test_%.o bin/libfiber.so
	$(CC) -Werror $(LDFLAGS) $(CFLAGS) -L. -Lbin $^ -o $@ -lpthread $(LDFLAGSAFTER)

#no -Werror for ev.c
bin/ev.o: ev.c
	$(CC) $(CFLAGS) -c $< -o $@

#no -Werror for ev.c
bin/ev.pic.o: ev.c $(INCLUDES)
	$(CC) $(CFLAGS) -DEV_STANDALONE -DSHARED_LIB -fPIC -c $< -o $@

bin/%.o: %.c $(INCLUDES)
	$(CC) -Werror -DEV_STANDALONE $(CFLAGS) -c $< -o $@

bin/%.pic.o: %.c $(INCLUDES)
	$(CC) -Werror $(CFLAGS) -DSHARED_LIB -fPIC -c $< -o $@

.PHONY:
coveragereport: CFLAGS += -fprofile-arcs -ftest-coverage
coveragereport: LDFLAGS += -lgcov
coveragereport: runtests
	cp -rt bin/ include/ src/ test/
	- lcov --directory `pwd` --capture --output-file bin/app.info
	- mkdir -p bin/lcov
	genhtml bin/app.info -o bin/lcov

clean:
	rm -rf bin/* libfiber.so



all: libfiber.so libfiber_pthread.so runtests

VPATH += src test

CFILES = \
    fiber_context.c \
    fiber_manager.c \
    fiber_mutex.c \
    fiber_cond.c \
    fiber.c \
    fiber_barrier.c \
    fiber_io.c \
    fiber_event.c \
    work_stealing_deque.c \

PTHREAD_CFILES = \
    fiber_pthread.c \

LDFLAGS += -lev

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

CFLAGS += -Werror -Wall -Iinclude -ggdb -O0

USE_VALGRIND ?= 0
ifeq ($(USE_VALGRIND),1)
CLFAGS += -DUSE_VALGRIND
endif

ifeq ($(OS),Darwin)
USE_COMPILER_THREAD_LOCAL ?= 0
LDFLAGS += -read_only_relocs suppress
FAST_SWITCHING ?= 0
endif

ifeq ($(OS),SunOS)
CFLAGS += -DSOLARIS
LINKER_SHARED_FLAG ?= -G
LDFLAGS += -lrt
endif

ifeq ($(OS),Linux)
CFLAGS += -DLINUX
LDFLAGSAFTER += -ldl -lev
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
    test_io \
    test_context \
    test_context_speed \
    test_basic \
    test_multithread \
    test_mpmc \
    test_spsc \
    test_mpsc \
    test_mpscr \
    test_wsd \
    test_mutex \
    test_wait_in_queue \
    test_cond \
    test_barrier \

#    test_pthread_cond \

CC ?= /usr/bin/c99

OBJS = $(patsubst %.c,bin/%.o,$(CFILES))
PICOBJS = $(patsubst %.c,bin/%.pic.o,$(CFILES))
PTHREAD_OBJS = $(patsubst %.c,bin/%.o,$(CFILES) $(PTHREAD_CFILES))
PTHREAD_PICOBJS = $(patsubst %.c,bin/%.pic.o,$(CFILES) $(PTHREAD_CFILES))
TESTBINARIES = $(patsubst %,bin/%,$(TESTS))
INCLUDES = $(wildcard include/*.h)
TESTINCLUDES = $(wildcard test/*.h)

libfiber.so: $(PICOBJS)
	$(CC) $(LINKER_SHARED_FLAG) $(LDFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGSAFTER)

libfiber_pthread.so: $(PTHREAD_PICOBJS)
	$(CC) $(LINKER_SHARED_FLAG) $(LDFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGSAFTER)

tests: $(TESTBINARIES)

runtests: tests
	for cur in $(TESTS); do echo $$cur; LD_LIBRARY_PATH=..:$$LD_LIBRARY_PATH time ./bin/$$cur > /dev/null; if [ "$$?" -ne "0" ] ; then echo "ERROR $$cur - failed!"; fi; done

bin/test_%.o: test_%.c $(INCLUDES) $(TESTINCLUDES)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

bin/test_%: bin/test_%.o libfiber.so
	$(CC) $(LDFLAGS) $(CFLAGS) -L. -Lbin $^ -o $@ -lpthread

bin/%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c $< -o $@

bin/%.pic.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -DSHARED_LIB -fPIC -c $< -o $@

clean:
	rm -f bin/* libfiber.so libfiber_pthread.so


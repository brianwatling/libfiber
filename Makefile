
all: libfiber2.so runtests

VPATH += src test

CFILES = \
    fiber_context.c \
    fiber_manager.c \
    fiber.c \
    work_stealing_deque.c \

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

FAST_SWITCHING ?= 1
ifeq ($(FAST_SWITCHING),1)
CFLAGS += -DFIBER_FAST_SWITCHING
endif

CFLAGS += -Werror -Wall -Iinclude -ggdb -O0

USE_VALGRIND ?= 0
ifeq ($(USE_VALGRIND),1)
CLFAGS += -DUSE_VALGRIND
endif

ifeq ($(OS),Darwin)
USE_COMPILER_THREAD_LOCAL ?= 0
endif
USE_COMPILER_THREAD_LOCAL ?= 1

ifeq ($(USE_COMPILER_THREAD_LOCAL),1)
CFLAGS += -DUSE_COMPILER_THREAD_LOCAL
endif

ifeq ($(OS),SunOS)
LINKER_SHARED_FLAG ?= -G
endif
LINKER_SHARED_FLAG ?= -shared

LDFLAGS += -lpthread

TESTS= \
    test_context \
    test_context_speed \
    test_basic \
    test_multithread \
    test_mpmc \
    test_spsc \
    test_mpsc \
    test_wsd \

CC ?= /usr/bin/c99

OBJS = $(patsubst %.c,bin/%.o,$(CFILES))
PICOBJS = $(patsubst %.c,bin/%.pic.o,$(CFILES))
TESTBINARIES = $(patsubst %,bin/%,$(TESTS))
INCLUDES = $(wildcard include/*.h)
TESTINCLUDES = $(wildcard test/*.h)

libfiber2.so: $(PICOBJS)
	$(CC) $(LINKER_SHARED_FLAG) $(LDFLAGS) $(CFLAGS) $^ -o $@

tests: $(TESTBINARIES)

runtests: tests
	for cur in $(TESTS); do echo $$cur; time ./bin/$$cur; done

bin/test_%.o: test_%.c $(INCLUDES) $(TESTINCLUDES)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

bin/test_%: bin/test_%.o libfiber2.so
	$(CC) $(LDFLAGS) $(CFLAGS) -L. -Lbin $^ -o $@

bin/%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c $< -o $@

bin/%.pic.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -DSHARED_LIB -fPIC -c $< -o $@

clean:
	rm -f bin/* libfiber2.so


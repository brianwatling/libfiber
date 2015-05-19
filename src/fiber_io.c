/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "fiber_io.h"
#include "fiber.h"
#include "fiber_manager.h"
#include "fiber_event.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/uio.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <dlfcn.h>

#if defined(SOLARIS)

#include <sys/filio.h>

int __xnet_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    return connect(sockfd, addr, addrlen);
}

ssize_t __xnet_recvmsg(int sockfd, struct msghdr* msg, int flags)
{
    return recvmsg(sockfd, msg, flags);
}

ssize_t __xnet_sendmsg(int sockfd, const struct msghdr* msg, int flags)
{
    return sendmsg(sockfd, msg, flags);
}

ssize_t __xnet_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen)
{
    return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

int __xnet_socket(int domain, int type, int protocol)
{
    return socket(domain, type, protocol);
}

int __xnet_socketpair(int domain, int type, int protocol, int sv[2])
{
    return socketpair(domain, type, protocol, sv);
}

typedef int (*acceptFnType) (int s, struct sockaddr* _RESTRICT_KYWD addr, Psocklen_t addrlen);
#define ACCEPTPARAMS int sockfd, struct sockaddr* _RESTRICT_KYWD addr, Psocklen_t addrlen

typedef ssize_t (*recvfromFnType)(int, void* _RESTRICT_KYWD, size_t, int, struct sockaddr* _RESTRICT_KYWD, Psocklen_t);
#define RECVFROMPARAMS int sockfd, void* _RESTRICT_KYWD buf, size_t len, int flags, struct sockaddr* _RESTRICT_KYWD src_addr, Psocklen_t addrlen

typedef int (*ioctlFnType)(int d, int request, ...);
#define IOCTLPARAMS int d, int request, ...

#elif defined(LINUX)

typedef int (*acceptFnType) (int, struct sockaddr*, socklen_t*);
#define ACCEPTPARAMS int sockfd, struct sockaddr* addr, socklen_t* addrlen

typedef ssize_t (*recvfromFnType)(int, void*, size_t, int, struct sockaddr*, socklen_t* );
#define RECVFROMPARAMS int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen

typedef int (*ioctlFnType)(int d, unsigned long int request, ...);
#define IOCTLPARAMS int d, unsigned long int request, ...

#else

#error unsupported OS

#endif

typedef int (*openFnType)(const char*, int, ...);
typedef int (*fcntlFnType)(int fd, int cmd, ...);
typedef int (*pipeFnType)(int pipefd[2]);
typedef ssize_t (*readFnType) (int, void*, size_t);
typedef ssize_t (*readvFnType)(int, const struct iovec*, int);
typedef ssize_t (*writeFnType) (int, const void*, size_t);
typedef ssize_t (*writevFnType) (int, const struct iovec*, int);
typedef int (*selectFnType)(int, fd_set*, fd_set*, fd_set*, struct timeval*);
typedef int (*pollFnType)(struct pollfd* fds, nfds_t nfds, int timeout);
typedef int (*socketFnType)(int socket_family, int socket_type, int protocol);
typedef int (*socketpairFnType)(int domain, int type, int protocol, int sv[2]);
typedef int (*connectFnType)(int, const struct sockaddr*, socklen_t);
typedef ssize_t (*sendFnType)(int, const void*, size_t, int);
typedef ssize_t (*sendtoFnType)(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);
typedef ssize_t (*sendmsgFnType)(int sockfd, const struct msghdr* msg, int flags);
typedef ssize_t (*recvFnType)(int, void*, size_t, int);
typedef ssize_t (*recvmsgFnType)(int sockfd, struct msghdr* msg, int flags);
typedef int (*closeFnType)(int fd);

/*static openFnType fibershim_open = NULL;
static pollFnType fibershim_poll = NULL;
static selectFnType fibershim_select = NULL;*/
static readFnType fibershim_read = NULL;
static readvFnType fibershim_readv = NULL;
static writeFnType fibershim_write = NULL;
static writevFnType fibershim_writev = NULL;
static socketFnType fibershim_socket = NULL;
static socketpairFnType fibershim_socketpair = NULL;
static acceptFnType fibershim_accept = NULL;
static sendFnType fibershim_send = NULL;
static sendtoFnType fibershim_sendto = NULL;
static sendmsgFnType fibershim_sendmsg = NULL;
static recvfromFnType fibershim_recvfrom = NULL;
static recvFnType fibershim_recv = NULL;
static recvmsgFnType fibershim_recvmsg = NULL;
static connectFnType fibershim_connect = NULL;
static pipeFnType fibershim_pipe = NULL;
static fcntlFnType fibershim_fcntl = NULL;
static ioctlFnType fibershim_ioctl = NULL;
static closeFnType fibershim_close = NULL;

#define STRINGIFY(x) XSTRINGIFY(x)
#define XSTRINGIFY(x) #x

selectFnType get_select_fn()
{
#if FD_SETSIZE > 1024 && !defined(_LP64) && defined(__PRAGMA_REDEFINE_EXTNAME)
    return (selectFnType)dlsym(RTLD_NEXT, "select_large_fdset");
#endif

#ifdef select
    if (strcmp(STRINGIFY(select), "select_large_fdset") == 0) {
        return (selectFnType)dlsym(RTLD_NEXT, "select_large_fdset");
    }
#endif
    return (selectFnType)dlsym(RTLD_NEXT, "select");
}

#define IO_FLAG_BLOCKING 1
#define IO_FLAG_WAITABLE 2

typedef struct fiber_fd_info
{
    volatile uint8_t flags_;
} fiber_fd_info_t;

static fiber_fd_info_t* fd_info = NULL;
static rlim_t max_fd = 0;

int fiber_io_init()
{
    //fibershim_open = (openFnType)dlsym(RTLD_NEXT, "open");
    fibershim_pipe = (pipeFnType)dlsym(RTLD_NEXT, "pipe");
    fibershim_read = (readFnType)dlsym(RTLD_NEXT, "read");
    fibershim_readv = (readvFnType)dlsym(RTLD_NEXT, "readv");
    fibershim_write = (writeFnType)dlsym(RTLD_NEXT, "write");
    fibershim_writev = (writevFnType)dlsym(RTLD_NEXT, "writev");
    //fibershim_select = get_select_fn();
    //fibershim_poll = (pollFnType)dlsym(RTLD_NEXT,"poll");
    fibershim_socket = (socketFnType)dlsym(RTLD_NEXT, "socket");
    fibershim_socketpair = (socketpairFnType)dlsym(RTLD_NEXT, "socketpair");
    fibershim_connect = (connectFnType)dlsym(RTLD_NEXT, "connect");
    fibershim_accept = (acceptFnType)dlsym(RTLD_NEXT, "accept");
    fibershim_send = (sendFnType)dlsym(RTLD_NEXT, "send");
    fibershim_sendto = (sendtoFnType)dlsym(RTLD_NEXT, "sendto");
    fibershim_sendmsg = (sendmsgFnType)dlsym(RTLD_NEXT, "sendmsg");
    fibershim_recv = (recvFnType)dlsym(RTLD_NEXT, "recv");
    fibershim_recvfrom = (recvfromFnType)dlsym(RTLD_NEXT, "recvfrom");
    fibershim_recvmsg = (recvmsgFnType)dlsym(RTLD_NEXT, "recvmsg");
    fibershim_close = (closeFnType)dlsym(RTLD_NEXT, "close");

    if(fd_info) {
        return FIBER_ERROR;
    }

    struct rlimit file_lim;
    if(getrlimit(RLIMIT_NOFILE, &file_lim)) {
        return FIBER_ERROR;
    }
    max_fd = file_lim.rlim_max;

    fd_info = calloc(max_fd, sizeof(*fd_info));
    if(!fd_info) {
        return FIBER_ERROR;
    }

    return FIBER_SUCCESS;
}

static __thread int thread_locked = 0;

int fiber_io_lock_thread()
{
    thread_locked = 1;
    return FIBER_SUCCESS;
}

int fiber_io_unlock_thread()
{
    thread_locked = 0;
    return FIBER_SUCCESS;
}

static inline int should_block(int fd)
{
    assert(fd >= 0);
    if(!thread_locked && fd_info && fd < max_fd && fd_info[fd].flags_ & (IO_FLAG_BLOCKING | IO_FLAG_WAITABLE)) {
        return 1;
    }
    return 0;
}

static int setup_socket(int sock)
{
    if(thread_locked) {
        return 0;
    }

    assert(sock < max_fd);
    __sync_fetch_and_or(&fd_info[sock].flags_, IO_FLAG_BLOCKING | IO_FLAG_WAITABLE);
    assert(fd_info[sock].flags_ & IO_FLAG_BLOCKING);
    assert(fd_info[sock].flags_ & IO_FLAG_WAITABLE);

    if(!fibershim_fcntl) {
        fibershim_fcntl = (fcntlFnType)dlsym(RTLD_NEXT, "fcntl");
    }

    const int ret = fibershim_fcntl(sock, F_SETFL, O_NONBLOCK);
    if(ret < 0) {
        return ret;
    }

    int on = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

int socket(int domain, int type, int protocol)
{
    if(!fibershim_socket) {
        fibershim_socket = (socketFnType)dlsym(RTLD_NEXT, "socket");
    }

    const int sock = fibershim_socket(domain, type, protocol);
    if(sock < 0) {
        return sock;
    }

    if(setup_socket(sock) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    if(!fibershim_socketpair) {
        fibershim_socketpair = (socketpairFnType)dlsym(RTLD_NEXT, "socketpair");
    }

    const int ret = fibershim_socketpair(domain, type, protocol, sv);
    if(!ret) {
        if(setup_socket(sv[0]) || setup_socket(sv[1])) {
            close(sv[0]);
            close(sv[1]);
            return -1;
        }
    }

    return ret;
}

int accept(ACCEPTPARAMS)
{
    if(!fibershim_accept) {
        fibershim_accept = (acceptFnType)dlsym(RTLD_NEXT, "accept");
    }

    int sock = fibershim_accept(sockfd, addr, addrlen);
    if(sock < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && should_block(sockfd)) {
        if(!fiber_wait_for_event(sockfd, FIBER_POLL_IN)) {
            return -1;
        }

        sock = fibershim_accept(sockfd, addr, addrlen);
    }

    if(sock > 0) {
        if(setup_socket(sock) < 0) {
            close(sock);
            return -1;
        }
    }

    return sock;
}

ssize_t read(int fd, void* buf, size_t count)
{
    if(!fibershim_read) {
        fibershim_read = (readFnType)dlsym(RTLD_NEXT, "read");
    }

    int ret;
    do {
        if(should_block(fd)) {
            if(!fiber_wait_for_event(fd, FIBER_POLL_IN)) {
                return -1;
            }
        }
        ret = fibershim_read(fd, buf, count);
    } while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && should_block(fd));

    return ret;
}

ssize_t readv(int fd, const struct iovec* iov, int iovcnt)
{
    if(!fibershim_readv) {
        fibershim_readv = (readvFnType)dlsym(RTLD_NEXT, "readv");
    }

    int ret;
    do {
        if(should_block(fd)) {
            if(!fiber_wait_for_event(fd, FIBER_POLL_IN)) {
                return -1;
            }
        }
        ret = fibershim_readv(fd, iov, iovcnt);
    } while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && should_block(fd));

    return ret;
}

ssize_t recv(int fd, void* buf, size_t len, int flags)
{
    if(!fibershim_recv) {
        fibershim_recv = (recvFnType)dlsym(RTLD_NEXT, "recv");
    }

    int ret;
    do {
        if(!(flags & MSG_DONTWAIT) && should_block(fd)) {
            if(!fiber_wait_for_event(fd, FIBER_POLL_IN)) {
                return -1;
            }
        }
        ret = fibershim_recv(fd, buf, len, flags);
    } while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(fd));

    return ret;
}

ssize_t recvfrom(RECVFROMPARAMS)
{
    if(!fibershim_recvfrom) {
        fibershim_recvfrom = (recvfromFnType)dlsym(RTLD_NEXT, "recvfrom");
    }

    int ret;
    do {
        if(!(flags & MSG_DONTWAIT) && should_block(sockfd)) {
            if(!fiber_wait_for_event(sockfd, FIBER_POLL_IN)) {
                return -1;
            }
        }
        ret = fibershim_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    } while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(sockfd));

    return ret;
}

ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags)
{
    if(!fibershim_recvmsg) {
        fibershim_recvmsg = (recvmsgFnType)dlsym(RTLD_NEXT, "recvmsg");
    }

    int ret;
    do {
        if(!(flags & MSG_DONTWAIT) && should_block(sockfd)) {
            if(!fiber_wait_for_event(sockfd, FIBER_POLL_IN)) {
                return -1;
            }
        }
        ret = fibershim_recvmsg(sockfd, msg, flags);
    } while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(sockfd));

    return ret;
}

ssize_t write(int fd, const void* buf, size_t count)
{
    if(!fibershim_write) {
        fibershim_write = (writeFnType)dlsym(RTLD_NEXT, "write");
    }

    int ret = fibershim_write(fd, buf, count);
    while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && should_block(fd)) {
        if(!fiber_wait_for_event(fd, FIBER_POLL_OUT)) {
            return -1;
        }
        ret = fibershim_write(fd, buf, count);
    }

    return ret;
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt)
{
    if(!fibershim_writev) {
        fibershim_writev = (writevFnType)dlsym(RTLD_NEXT, "writev");
    }

    int ret = fibershim_writev(fd, iov, iovcnt);
    while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && should_block(fd)) {
        if(!fiber_wait_for_event(fd, FIBER_POLL_OUT)) {
            return -1;
        }
        ret = fibershim_writev(fd, iov, iovcnt);
    }

    return ret;
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags)
{
    if(!fibershim_send) {
        fibershim_send = (sendFnType)dlsym(RTLD_NEXT, "send");
    }

    ssize_t ret = fibershim_send(sockfd, buf, len, flags);
    while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(sockfd)) {
        if(!fiber_wait_for_event(sockfd, FIBER_POLL_OUT)) {
            return -1;
        }
        ret = fibershim_send(sockfd, buf, len, flags);
    }

    return ret;
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen)
{
    if(!fibershim_sendto) {
        fibershim_sendto = (sendtoFnType)dlsym(RTLD_NEXT, "sendto");
    }

    ssize_t ret = fibershim_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(sockfd)) {
        if(!fiber_wait_for_event(sockfd, FIBER_POLL_OUT)) {
            return -1;
        }
        ret = fibershim_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }

    return ret;
}

ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags)
{
    if(!fibershim_sendmsg) {
        fibershim_sendmsg = (sendmsgFnType)dlsym(RTLD_NEXT, "sendmsg");
    }

    ssize_t ret = fibershim_sendmsg(sockfd, msg, flags);
    while(ret < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(flags & MSG_DONTWAIT) && should_block(sockfd)) {
        if(!fiber_wait_for_event(sockfd, FIBER_POLL_OUT)) {
            return -1;
        }
        ret = fibershim_sendmsg(sockfd, msg, flags);
    }

    return ret;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    if(!fibershim_connect) {
        fibershim_connect = (connectFnType)dlsym(RTLD_NEXT, "connect");
    }

    int ret = fibershim_connect(sockfd, addr, addrlen);
    if(ret < 0 && errno == EINPROGRESS && should_block(sockfd))
    {
        if(!fiber_wait_for_event(sockfd, FIBER_POLL_OUT)) {
            return -1;
        }

        int so_error;
        socklen_t outSize = sizeof(so_error);
        if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &outSize)) {
            return -1;
        }

        if(so_error) {
            errno = so_error;
            return -1;
        }

        return 0;
    }

    return ret;
}

typedef unsigned int (*sleepFnType) (unsigned int);
typedef int (*usleepFnType) (useconds_t);
typedef int (*nanosleepFnType) (const struct timespec*,  struct timespec*);

static sleepFnType fibershim_sleep = NULL;
static usleepFnType fibershim_usleep = NULL;
static nanosleepFnType fibershim_nanosleep = NULL;

unsigned int sleep(unsigned int seconds)
{
     if(!thread_locked && fiber_manager_get()) {
         fiber_sleep(seconds, 0);
     } else {
         if(!fibershim_sleep) {
             fibershim_sleep = (sleepFnType)dlsym(RTLD_NEXT, "sleep");
         }
         fibershim_sleep(seconds);
     }
     return 0;
}

int usleep(useconds_t useconds)
{
     if(!thread_locked && fiber_manager_get()) {
         fiber_sleep(useconds / 1000000, useconds % 1000000);
     } else {
         if(!fibershim_usleep) {
             fibershim_usleep = (usleepFnType)dlsym(RTLD_NEXT, "usleep");
         }
         fibershim_usleep(useconds);
     }
     return 0;
}

int nanosleep(const struct timespec* rqtp,  struct timespec* rmtp)
{
     if(!thread_locked && fiber_manager_get()) {
         fiber_sleep(rqtp->tv_sec, rqtp->tv_nsec / 1000 + 1);
         if(rmtp) {
             rmtp->tv_sec = 0;
             rmtp->tv_nsec = 0;
         }
     } else {
         if(!fibershim_nanosleep) {
             fibershim_nanosleep = (nanosleepFnType)dlsym(RTLD_NEXT, 
"nanosleep");
         }
         fibershim_nanosleep(rqtp, rmtp);
     }
     return 0;
}

int pipe(int pipefd[2])
{
    if(!fibershim_pipe) {
        fibershim_pipe = (pipeFnType)dlsym(RTLD_NEXT, "pipe");
    }

    int ret = fibershim_pipe(pipefd);
    if(ret == 0 && fd_info && !thread_locked) {
        if(!fibershim_fcntl) {
            fibershim_fcntl = (fcntlFnType)dlsym(RTLD_NEXT, "fcntl");
        }

        ret = fibershim_fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        if(ret < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return ret;
        }

        ret = fibershim_fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
        if(ret < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return ret;
        }

        assert(pipefd[0] < max_fd);
        __sync_fetch_and_or(&fd_info[pipefd[0]].flags_, IO_FLAG_BLOCKING | IO_FLAG_WAITABLE);
        assert(fd_info[pipefd[0]].flags_ & IO_FLAG_BLOCKING);
        assert(fd_info[pipefd[0]].flags_ & IO_FLAG_WAITABLE);
        assert(pipefd[1] < max_fd);
        __sync_fetch_and_or(&fd_info[pipefd[1]].flags_, IO_FLAG_BLOCKING | IO_FLAG_WAITABLE);
        assert(fd_info[pipefd[1]].flags_ & IO_FLAG_BLOCKING);
        assert(fd_info[pipefd[1]].flags_ & IO_FLAG_WAITABLE);
    }

    return ret;
}

#ifdef __GNUC__
#define STATIC_ASSERT_HELPER(expr, msg) \
    (!!sizeof (struct { unsigned int STATIC_ASSERTION__##msg: (expr) ? 1 : -1; }))
#define STATIC_ASSERT(expr, msg) \
    extern int (*assert_function__(void)) [STATIC_ASSERT_HELPER(expr, msg)]
#else
    #define STATIC_ASSERT(expr, msg)   \
    extern char STATIC_ASSERTION__##msg[1]; \
    extern char STATIC_ASSERTION__##msg[(expr)?1:2]
#endif /* #ifdef __GNUC__ */

int fcntl(int fd, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    STATIC_ASSERT(sizeof(long) == sizeof(void*), fcntl_param_sizes_must_match);
    long val = va_arg(args, long);
    va_end(args);

    if(!thread_locked) {
        if(cmd == F_SETFL && (val == O_NONBLOCK || val == O_NDELAY)) {
            assert(fd < max_fd);
            __sync_fetch_and_and(&fd_info[fd].flags_, ~IO_FLAG_BLOCKING);
            assert(!(fd_info[fd].flags_ & IO_FLAG_BLOCKING));
            return 0;
        }
        //make sure O_NONBLOCK stays set
        if(cmd == F_SETFL) {
            val |= O_NONBLOCK;
        }
    }

    if(!fibershim_fcntl) {
        fibershim_fcntl = (fcntlFnType)dlsym(RTLD_NEXT, "fcntl");
    }

    return fibershim_fcntl(fd, cmd, val);
}

int ioctl(IOCTLPARAMS)
{
    va_list args;
    va_start(args, request);
    void* val = va_arg(args, void*);
    va_end(args);

    if(!thread_locked && request == FIONBIO) {
        if(!val) {
            errno = EINVAL;
            return -1;
        }
        assert(d < max_fd);
        if(*(int*)val) {
            __sync_fetch_and_and(&fd_info[d].flags_, ~IO_FLAG_BLOCKING);
            assert(!(fd_info[d].flags_ & IO_FLAG_BLOCKING));
        } else {
            __sync_fetch_and_or(&fd_info[d].flags_, IO_FLAG_BLOCKING);
            assert(fd_info[d].flags_ & IO_FLAG_BLOCKING);
        }
        return 0;
    }

    if(!fibershim_ioctl) {
        fibershim_ioctl = (ioctlFnType)dlsym(RTLD_NEXT, "ioctl");
    }

    return fibershim_ioctl(d, request, val);
}

int close(int fd)
{
    if(!fibershim_close) {
        fibershim_close = (closeFnType)dlsym(RTLD_NEXT, "close");
    }

    fiber_fd_closed(fd);
    if (fd_info && fd < max_fd) {
        fd_info[fd].flags_ = 0;
    }
    return fibershim_close(fd);
}


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
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <dlfcn.h>

#if defined(SOLARIS)

#include <sys/filio.h>

static const char* CONNECT_STRING = "__xnet_connect";
#define CONNECTFUNCTION __xnet_connect
static const char* RECVMSG_STRING = "__xnet_recvmsg";
#define RECVMSGFUNCTION __xnet_recvmsg
static const char* SENDMSG_STRING = "__xnet_sendmsg";
#define SENDMSGFUNCTION __xnet_sendmsg
static const char* SENDTO_STRING = "__xnet_sendto";
#define SENDTOFUNCTION __xnet_sendto
static const char* SOCKET_STRING = "__xnet_socket";
#define SOCKETFUNCTION __xnet_socket

//other functions maybe worth shimming:
//__xnet_socketpair
//__xnet_getaddrinfo

typedef int (*acceptFnType) (int s, struct sockaddr *_RESTRICT_KYWD addr, Psocklen_t addrlen);
#define ACCEPTPARAMS int sockfd, struct sockaddr *_RESTRICT_KYWD addr, Psocklen_t addrlen

typedef ssize_t (*recvfromFnType)(int, void* _RESTRICT_KYWD, size_t, int, struct sockaddr* _RESTRICT_KYWD, Psocklen_t);
#define RECVFROMPARAMS int sockfd, void* _RESTRICT_KYWD buf, size_t len, int flags, struct sockaddr* _RESTRICT_KYWD src_addr, Psocklen_t addrlen

typedef int (*ioctlFnType)(int d, int request, ...);
#define IOCTLPARAMS int d, int request, ...

#elif defined(LINUX)

static const char* SOCKET_STRING = "socket";
#define SOCKETFUNCTION socket
static const char* SENDMSG_STRING = "sendmsg";
#define SENDMSGFUNCTION sendmsg
static const char* SENDTO_STRING = "sendto";
#define SENDTOFUNCTION sendto
static const char* RECVMSG_STRING = "recvmsg";
#define RECVMSGFUNCTION recvmsg
static const char* CONNECT_STRING = "connect";
#define CONNECTFUNCTION connect

typedef int (*acceptFnType) (int, struct sockaddr*, socklen_t*);
#define ACCEPTPARAMS int sockfd, struct sockaddr* addr, socklen_t* addrlen

typedef ssize_t (*recvfromFnType)(int, void*, size_t, int, struct sockaddr*, socklen_t *);
#define RECVFROMPARAMS int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen

typedef int (*ioctlFnType)(int d, unsigned long int request, ...);
#define IOCTLPARAMS int d, unsigned long int request, ...

#else

#error unsupported OS

#endif

typedef int (*openFnType)(const char*, int, ...);
typedef int (*fcntlFnType)(int fd, int cmd, ...);
typedef int (*pipeFnType)(int pipefd[2]);
typedef ssize_t (*readFnType) (int, void *, size_t);
typedef ssize_t (*writeFnType) (int, const void*, size_t);
typedef int (*selectFnType)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
typedef int (*pollFnType)(struct pollfd *fds, nfds_t nfds, int timeout);
typedef int (*socketFnType)(int socket_family, int socket_type, int protocol);
typedef int (*connectFnType)(int, const struct sockaddr*, socklen_t);
typedef ssize_t (*sendFnType)(int, const void*, size_t, int);
typedef ssize_t (*sendtoFnType)(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
typedef ssize_t (*sendmsgFnType)(int sockfd, const struct msghdr *msg, int flags);
typedef ssize_t (*recvFnType)(int, void*, size_t, int);
typedef ssize_t (*recvmsgFnType)(int sockfd, struct msghdr *msg, int flags);
typedef int (*closeFnType)(int fd);

/*static openFnType fibershim_open = NULL;
static pollFnType fibershim_poll = NULL;
static selectFnType fibershim_select = NULL;*/
static readFnType fibershim_read = NULL;
static writeFnType fibershim_write = NULL;
static socketFnType fibershim_socket = NULL;
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
    return (selectFnType) dlsym(RTLD_NEXT, "select_large_fdset");
#endif

#ifdef select
    if (strcmp(STRINGIFY(select), "select_large_fdset") == 0) {
        return (selectFnType) dlsym(RTLD_NEXT, "select_large_fdset");
    }
#endif
    return (selectFnType) dlsym(RTLD_NEXT, "select");
}

typedef struct fiber_fd_info
{
    volatile uint8_t blocking;
} fiber_fd_info_t;

static fiber_fd_info_t* fd_info = NULL;
static rlim_t max_fd = 0;

int fiber_io_init()
{
    //fibershim_open = (openFnType)dlsym (RTLD_NEXT, "open");
    fibershim_pipe = (pipeFnType)dlsym (RTLD_NEXT, "pipe");
    fibershim_read = (readFnType)dlsym (RTLD_NEXT, "read");
    fibershim_write = (writeFnType)dlsym (RTLD_NEXT, "write");
    //fibershim_select = get_select_fn();
    //fibershim_poll = (pollFnType) dlsym(RTLD_NEXT,"poll");
    fibershim_socket = (socketFnType) dlsym(RTLD_NEXT, SOCKET_STRING);
    fibershim_connect = (connectFnType) dlsym(RTLD_NEXT, CONNECT_STRING);
    fibershim_accept = (acceptFnType) dlsym(RTLD_NEXT, "accept");
    fibershim_send = (sendFnType)dlsym (RTLD_NEXT, "send");
    fibershim_sendto = (sendtoFnType)dlsym (RTLD_NEXT, SENDTO_STRING);
    fibershim_sendmsg = (sendmsgFnType)dlsym (RTLD_NEXT, SENDMSG_STRING);
    fibershim_recv = (recvFnType)dlsym (RTLD_NEXT, "recv");
    fibershim_recvfrom = (recvfromFnType)dlsym (RTLD_NEXT, "recvfrom");
    fibershim_recvmsg = (recvmsgFnType)dlsym (RTLD_NEXT, RECVMSG_STRING);
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

static inline int should_block(int fd)
{
    assert(fd >= 0);
    if(fd_info && fd < max_fd && fd_info[fd].blocking) {
        return 1;
    }
    return 0;
}

static int setup_socket(int sock)
{
    assert(sock < max_fd);
    fd_info[sock].blocking = 1;

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

int SOCKETFUNCTION(int domain, int type, int protocol)
{
    if(!fibershim_socket) {
        fibershim_socket = (socketFnType)dlsym(RTLD_NEXT, SOCKET_STRING);
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

int accept(ACCEPTPARAMS)
{
    if(!fibershim_accept) {
        fibershim_accept = (acceptFnType) dlsym(RTLD_NEXT, "accept");
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

ssize_t RECVMSGFUNCTION(int sockfd, struct msghdr* msg, int flags)
{
    if(!fibershim_recvmsg) {
        fibershim_recvmsg = (recvmsgFnType)dlsym(RTLD_NEXT, RECVMSG_STRING);
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

ssize_t SENDTOFUNCTION(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen)
{
    if(!fibershim_sendto) {
        fibershim_sendto = (sendtoFnType)dlsym(RTLD_NEXT, SENDTO_STRING);
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

ssize_t SENDMSGFUNCTION(int sockfd, const struct msghdr* msg, int flags)
{
    if(!fibershim_sendmsg) {
        fibershim_sendmsg = (sendmsgFnType)dlsym(RTLD_NEXT, SENDMSG_STRING);
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

int CONNECTFUNCTION(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    if(!fibershim_connect) {
        fibershim_connect = (connectFnType)dlsym(RTLD_NEXT, CONNECT_STRING);
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

unsigned int sleep(unsigned int seconds)
{
    fiber_sleep(seconds, 0);
    return 0;
}

int usleep(useconds_t useconds)
{
    fiber_sleep(useconds / 1000000, useconds % 1000000);
    return 0;
}

int nanosleep(const struct timespec *rqtp,  struct timespec *rmtp)
{
    fiber_sleep(rqtp->tv_sec, rqtp->tv_nsec / 1000 + 1);
    if(rmtp) {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

int pipe(int pipefd[2])
{
    if(!fibershim_pipe) {
        fibershim_pipe = (pipeFnType)dlsym(RTLD_NEXT, "pipe");
    }

    int ret = fibershim_pipe(pipefd);
    if(ret == 0 && fd_info) {
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
        fd_info[pipefd[0]].blocking = 1;
        assert(pipefd[1] < max_fd);
        fd_info[pipefd[1]].blocking = 1;
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

    if(cmd == F_SETFL && (val == O_NONBLOCK || val == O_NDELAY)) {
        assert(fd < max_fd);
        fd_info[fd].blocking = 0;
        return 0;
    }
    //make sure O_NONBLOCK stays set
    if(cmd == F_SETFL) {
        val |= O_NONBLOCK;
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

    if(request == FIONBIO) {
        if(!val) {
            errno = EINVAL;
            return -1;
        }
        assert(d < max_fd);
        fd_info[d].blocking = *(int*)val ? 0 : 1;
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
    return fibershim_close(fd);
}


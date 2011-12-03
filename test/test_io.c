#include "fiber_manager.h"
#include "test_helper.h"
#include "fiber_event.h"
#include "fiber_io.h"
#include "fiber_barrier.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#define NUM_THREADS 1
#define NUM_FIBERS 4

#ifdef LINUX
const char* LOCALHOST = "0.0.0.0";
#else
const char* LOCALHOST = "localhost";
#endif

fiber_barrier_t barrier;

void* server_function(void* param)
{
    int acceptCount = NUM_FIBERS;
    struct addrinfo hints, *res;
    int sockfd;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;// use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;// fill in my IP for me
    getaddrinfo(LOCALHOST, "10000", &hints, &res);

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    test_assert(!bind(sockfd, res->ai_addr, res->ai_addrlen));
    listen(sockfd, 50);

    fiber_barrier_wait(&barrier);

    int totalBytes = 0;
    int sock = 0;
    while(acceptCount > 0 && (sock = accept(sockfd, NULL, NULL)) >= 0)
    {
        char msg[5];
        struct sockaddr src_addr;
        socklen_t addrlen;
        int ret = recv(sock, msg, sizeof(msg), 0);
        totalBytes += ret;
        totalBytes += recvfrom(sock, msg, sizeof(msg), 0, &src_addr, &addrlen);
        --acceptCount;
        printf("%d\n", acceptCount);
        close(sock);
    }

    close(sockfd);

    test_assert(totalBytes = NUM_FIBERS * 2 * 5);

    return NULL;
}

void* client_function(void* param)
{
    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;// use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;// fill in my IP for me
    getaddrinfo(LOCALHOST, "10000", &hints, &res);

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    test_assert(sockfd >= 0);
    const int ret = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if(ret) {
        printf("connect failed: %s\n", strerror(errno));
    }
    test_assert(!ret);

    send(sockfd, "hello", 5, 0);
    sendto(sockfd, "hello", 5, 0, NULL, 0);
    return NULL;
}

int main()
{
    fiber_manager_set_total_kernel_threads(NUM_THREADS);
    fiber_io_init();
    fiber_event_init();

    fiber_barrier_init(&barrier, 2);

    fiber_t* server = fiber_create(100000, &server_function, NULL);
    fiber_barrier_wait(&barrier);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(100000, &client_function, NULL);
    }

    fiber_join(server, NULL);
    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    fiber_barrier_destroy(&barrier);

    fiber_event_destroy();

    return 0;
}


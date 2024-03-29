// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "fiber_barrier.h"
#include "fiber_event.h"
#include "fiber_io.h"
#include "fiber_manager.h"
#include "test_helper.h"

#define NUM_THREADS 1
#define NUM_FIBERS 200

#ifdef __linux__
const char* LOCALHOST = "0.0.0.0";
#else
const char* LOCALHOST = "localhost";
#endif

fiber_barrier_t barrier;

void* server_function(void* param) {
  int acceptCount = 0;
  struct addrinfo hints, *res;
  int sockfd;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // fill in my IP for me
  getaddrinfo(LOCALHOST, "10000", &hints, &res);

  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  test_assert(!bind(sockfd, res->ai_addr, res->ai_addrlen));
  listen(sockfd, 100);

  fiber_barrier_wait(&barrier);

  int totalBytes = 0;
  int sock = 0;
  while (acceptCount < NUM_FIBERS && (sock = accept(sockfd, NULL, NULL)) >= 0) {
    char msg[5];
    struct sockaddr src_addr;
    socklen_t addrlen;
    int ret = recv(sock, msg, sizeof(msg), 0);
    test_assert(ret == 5);
    test_assert(5 == send(sock, "world", 5, 0));
    totalBytes += ret;
    ret = recvfrom(sock, msg, sizeof(msg), 0, &src_addr, &addrlen);
    test_assert(ret == 5);
    test_assert(5 == send(sock, "world", 5, 0));
    totalBytes += ret;
    ++acceptCount;
    printf("%d %d\n", acceptCount, totalBytes);
    close(sock);
  }

  close(sockfd);

  test_assert(totalBytes = NUM_FIBERS * 2 * 5);

  return NULL;
}

void* client_function(void* param) {
  struct addrinfo hints, *res;
  int sockfd;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // fill in my IP for me
  getaddrinfo(LOCALHOST, "10000", &hints, &res);

  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  test_assert(sockfd >= 0);
  const int ret = connect(sockfd, res->ai_addr, res->ai_addrlen);
  if (ret) {
    printf("connect failed: %s\n", strerror(errno));
  }
  test_assert(!ret);

  test_assert(5 == send(sockfd, "hello", 5, 0));
  char msg[5];
  test_assert(5 == recv(sockfd, msg, sizeof(msg), 0));
  test_assert(5 == sendto(sockfd, "hello", 5, 0, NULL, 0));
  test_assert(5 == recvfrom(sockfd, msg, sizeof(msg), 0, NULL, NULL));
  close(sockfd);
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_barrier_init(&barrier, 2);

  fiber_t* server = fiber_create(100000, &server_function, NULL);
  fiber_barrier_wait(&barrier);

  fiber_t* fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(100000, &client_function, NULL);
  }

  fiber_join(server, NULL);
  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  fiber_barrier_destroy(&barrier);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}

// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

void* runFunc(void* theSock) {
  timeval buffer[2];
  while (1) {
    int numBytes = 0;
    while (numBytes < sizeof(buffer[0])) {
      int ret = recv((intptr_t)theSock, &buffer[0], sizeof(buffer[0]), NULL);
      if (ret <= 0) {
        printf("errno: %d %s\n", errno, strerror(errno));
        assert(false);
      }
      numBytes += ret;
    }
    gettimeofday(&buffer[1], NULL);
    assert(sizeof(buffer) ==
           send((intptr_t)theSock, buffer, sizeof(buffer), NULL));
  }

  close((intptr_t)theSock);
}

int main() {
  struct addrinfo hints, *res;
  int sockfd;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // fill in my IP for me
  getaddrinfo(NULL, "10000", &hints, &res);

  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  bind(sockfd, res->ai_addr, res->ai_addrlen);
  listen(sockfd, 5);

  int sock = 0;
  while ((sock = accept(sockfd, NULL, NULL)) >= 0) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 102400);
    pthread_t newThread;
    assert(!pthread_create(&newThread, &attr, &runFunc, (void*)sock));
    pthread_detach(newThread);

    pthread_attr_destroy(&attr);
  }

  return 0;
}

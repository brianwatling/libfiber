// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

long long getusecs(struct timeval* tv) {
  return (long long)tv->tv_sec * 1000000 + tv->tv_usec;
}

int reqsPerSec = 0;

void* runFunc(void* param) {
  struct addrinfo hints, *res;
  int sockfd;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;  // fill in my IP for me
  getaddrinfo(NULL, "10000", &hints, &res);

  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  assert(sockfd >= 0);
  assert(!connect(sockfd, res->ai_addr, res->ai_addrlen));

  unsigned int seed = (unsigned int)(intptr_t)param;
  timeval buffer[2];
  while (1) {
    gettimeofday(&buffer[0], NULL);
    assert(sizeof(buffer[0]) ==
           send(sockfd, &buffer[0], sizeof(buffer[0]), NULL));
    int amnt = 0;
    while (amnt < sizeof(buffer)) {
      int ret = recv(sockfd, buffer, sizeof(buffer), NULL);
      if (ret <= 0) {
        printf("errno: %d %s\n", errno, strerror(errno));
        assert(false);
      }
      amnt += ret;
    }
    long long diff = getusecs(&buffer[1]) - getusecs(&buffer[0]);
    // printf("diff: %lf\n", (double)diff * 0.000001);

    __sync_fetch_and_add(&reqsPerSec, 1);
  }

  close(sockfd);
}

void* statFunc(void*) {
  while (1) {
    sleep(1);
    printf("count: %d\n", reqsPerSec);
    reqsPerSec = 0;
  }
}

int main(int argc, char* argv[]) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 102400);
  pthread_t newThread;
  pthread_create(&newThread, &attr, &statFunc, NULL);
  pthread_detach(newThread);
  intptr_t count = atoi(argv[1]);
  for (intptr_t i = 0; i < count; ++i) {
    assert(!pthread_create(&newThread, &attr, &runFunc, (void*)i));
    pthread_detach(newThread);
  }

  runFunc(NULL);

  pthread_attr_destroy(&attr);

  return 0;
}

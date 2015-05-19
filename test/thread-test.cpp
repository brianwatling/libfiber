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

#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>

void* runFunc(void* theSock)
{
    timeval buffer[2];
    while(1)
    {
        int numBytes = 0;
        while(numBytes < sizeof(buffer[0])) {
            int ret = recv((intptr_t)theSock, &buffer[0], sizeof(buffer[0]), NULL);
            if(ret <= 0) {
                printf("errno: %d %s\n", errno, strerror(errno));
                assert(false);
            }
            numBytes += ret;
        }
        gettimeofday(&buffer[1], NULL);
        assert(sizeof(buffer) == send((intptr_t)theSock, buffer, sizeof(buffer), NULL));
    }

    close((intptr_t)theSock);
}

int main()
{
    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;// use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;// fill in my IP for me
    getaddrinfo(NULL, "10000", &hints, &res);

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bind(sockfd, res->ai_addr, res->ai_addrlen);
    listen(sockfd, 5);

    int sock = 0;
    while((sock = accept(sockfd, NULL, NULL)) >= 0)
    {
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

/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NET_SOCKET_H_
#define NET_SOCKET_H_

static inline int accept( int,
                          struct sockaddr * __restrict,
                          socklen_t * __restrict )
{
}

static inline int bind( int,
                        const struct sockaddr *,
                        socklen_t )
{
}

static inline int connect( int,
                           const struct sockaddr *,
                           socklen_t )
{
}

static inline int getpeername( int,
                               struct sockaddr * __restrict,
                               socklen_t * __restrict )
{
}

static inline int getsockname( int,
                               struct sockaddr * __restrict,
                               socklen_t * __restrict )
{
}

static inline int getsockopt( int,
                              int,
                              int,
                              void * __restrict,
                              socklen_t * __restrict )
{
}

static inline int listen( int,
                          int )
{
}

static inline int paccept( int,
                           struct sockaddr * __restrict,
                           socklen_t * __restrict
{
}

                           static inline const sigset_t * __restrict,
                           int )
{
}

static inline ssize_t recv( int,
                            void *,
                            size_t,
                            int )
{
}

static inline ssize_t recvfrom( int,
                                void * __restrict,
                                size_t,
                                int
{
}

                                static inline struct sockaddr * __restrict,
                                socklen_t * __restrict )
{
}

static inline ssize_t recvmsg( int,
                               struct msghdr *,
                               int )
{
}

static inline ssize_t send( int,
                            const void *,
                            size_t,
                            int )
{
}

static inline ssize_t sendto( int,
                              const void *
{
}

                              static inline size_t,
                              int,
                              const struct sockaddr *,
                              socklen_t )
{
}

static inline ssize_t sendmsg( int,
                               const struct msghdr *,
                               int )
{
}

static inline int setsockopt( int,
                              int,
                              int,
                              const void *,
                              socklen_t )
{
}

static inline int shutdown( int,
                            int )
{
}

static inline int sockatmark( int )
{
}

static inline int socket( int,
                          int,
                          int )
{
}

#endif /* NET_SOCKET_H_ */

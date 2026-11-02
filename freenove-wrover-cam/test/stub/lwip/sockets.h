/* Host stand-in (test only): lwip's socket calls by lwip's own names (send()
 * is lwip_send(), as lwip maps them), answered by the test, which keeps what
 * is sent. The host's fd_set is used where it has one, as lwip does. */
#pragma once
#include <stddef.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(_WIN32)
typedef long suseconds_t;
#endif
#define AF_UNSPEC     0
#define SOCK_STREAM   1
#define SOL_SOCKET    0xfff
#define SO_KEEPALIVE  0x0008
#define SO_SNDTIMEO   0x1005
#define IPPROTO_TCP   6
#define TCP_KEEPIDLE  0x03
#define TCP_KEEPINTVL 0x04
#define TCP_KEEPCNT   0x05
struct lwip_sockaddr {
    int sa_family;
};
#define sockaddr lwip_sockaddr
#ifndef FD_SET
typedef struct {
    unsigned char bits[8];
} fd_set;
#define FD_ZERO(set)   memset((set), 0, sizeof(*(set)))
#define FD_SET(n, set) ((set)->bits[(n) / 8] |= (unsigned char)(1u << ((n) % 8)))
#endif
int lwip_socket(int domain, int type, int protocol);
int lwip_connect(int s, const struct sockaddr *name, unsigned namelen);
int lwip_setsockopt(int s, int level, int name, const void *value, unsigned len);
int lwip_send(int s, const void *data, size_t size, int flags);
int lwip_recv(int s, void *mem, size_t len, int flags);
int lwip_select(int n, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout);
#define socket(d, t, p)          lwip_socket(d, t, p)
#define connect(s, a, n)         lwip_connect(s, a, n)
#define setsockopt(s, l, o, v, n) lwip_setsockopt(s, l, o, v, n)
#define send(s, d, n, f)         lwip_send(s, d, n, f)
#define recv(s, m, n, f)         lwip_recv(s, m, n, f)
#define select(n, r, w, e, t)    lwip_select(n, r, w, e, t)

/* Host stand-in (test only): name lookups, answered by the test. */
#pragma once
#include "lwip/sockets.h"
struct lwip_addrinfo {
    int                   ai_flags;
    int                   ai_family;
    int                   ai_socktype;
    int                   ai_protocol;
    unsigned              ai_addrlen;
    struct sockaddr      *ai_addr;
    char                 *ai_canonname;
    struct lwip_addrinfo *ai_next;
};
#define addrinfo lwip_addrinfo
int  lwip_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void lwip_freeaddrinfo(struct addrinfo *ai);
#define getaddrinfo(n, s, h, r) lwip_getaddrinfo(n, s, h, r)
#define freeaddrinfo(ai)        lwip_freeaddrinfo(ai)

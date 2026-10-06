/*
 * warblet_open.c - the Warblet Open starter in portable C, on the sockets API.
 *
 * Every 10 seconds your loop calls warblet_open_step(). It asks Warblet for a
 * command, then posts one line of plain text, "<count> <what it last heard>",
 * like "3 hello". Type in the Message box on the board's device page and the
 * next line says it back. Open level: no token, no TLS, no clock.
 *
 *     char line[1040];
 *     for (;;) {                                  // once your network is up
 *         int status = warblet_open_step("<your-board-id>", line, sizeof line);
 *         printf("%s -> %d\n", line, status);     // 202: Warblet has it
 *         sleep(10);                              // or your RTOS's delay
 *     }
 *
 * The id is yours to choose: letters, digits and - _ . : (up to 128). Anyone
 * who knows it can post as this board, so add something hard to guess.
 * Sockets come from <sys/socket.h>, or from lwIP when built with
 * -DWARBLET_LWIP. It has run on ESP-IDF 5.5 (ESP32-C5), where it builds as
 * it is, and on an NXP RW612 with the MCUXpresso SDK's lwIP (-DWARBLET_LWIP).
 * MIT licence, Robota LLC.
 */
#include <stdio.h>
#include <string.h>
#ifdef WARBLET_LWIP
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#define close_socket lwip_close   /* close() is lwIP's only with its POSIX names on */
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define close_socket close
#endif

#define WARBLET_HTTP_HOST "http.warbletiot.com"   /* plain HTTP, port 80 */
#define WARBLET_UDP_HOST  "udp.warbletiot.com"    /* port 7701: nothing comes back */

static char buf[1600];        /* one request, or one answer, at a time */

/* A TCP or UDP socket to host:port; each wait on it gives up after 15 s. */
static int open_socket(const char *host, const char *port, int type)
{
    struct addrinfo hints, *found = NULL;
    struct timeval limit = { 15, 0 };
    int s;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = type;
    if (getaddrinfo(host, port, &hints, &found) != 0 || found == NULL)
        return -1;                                /* the name lookup failed */
    s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s >= 0 && (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit) != 0 ||
                   setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit) != 0 ||
                   connect(s, found->ai_addr, found->ai_addrlen) != 0)) {
        close_socket(s);                          /* no time limit, or no connection */
        s = -1;
    }
    freeaddrinfo(found);
    return s;
}

/* One HTTP request. Returns the status (202, 200, 204, 429...) or -1 when the
   network failed. The answer's body is copied to reply, if reply is not NULL. */
static int http(const char *method, const char *path, const char *body,
                char *reply, size_t reply_size)
{
    int s, n, got = 0, status = -1;
    char *start;

    s = open_socket(WARBLET_HTTP_HOST, "80", SOCK_STREAM);
    if (s < 0)
        return -1;
    n = snprintf(buf, sizeof buf, "%s %s HTTP/1.0\r\nHost: %s\r\n"
                 "Content-Length: %u\r\n\r\n%s", method, path,
                 WARBLET_HTTP_HOST, (unsigned)strlen(body), body);
    if (n > 0 && n < (int)sizeof buf && send(s, buf, n, 0) == n) {
        while (got < (int)sizeof buf - 1) {       /* read until Warblet hangs up */
            n = recv(s, buf + got, sizeof buf - 1 - got, 0);
            if (n <= 0)
                break;
            got += n;
        }
    }
    close_socket(s);
    buf[got] = '\0';
    if (sscanf(buf, "HTTP/%*s %d", &status) != 1)
        return -1;
    start = strstr(buf, "\r\n\r\n");
    if (reply != NULL && reply_size > 0)
        snprintf(reply, reply_size, "%s", start != NULL ? start + 4 : "");
    return status;
}

/* Post one line of text. Returns the HTTP status: 202 means Warblet has it. */
int warblet_post(const char *id, const char *line)
{
    char path[160];

    snprintf(path, sizeof path, "/ingest/%s", id);
    return http("POST", path, line, NULL, 0);
}

/* Ask for a command (typed in the Message box, or sent by a routine).
   Returns 1 and fills text when one was waiting, 0 when none, -1 on failure. */
int warblet_get_command(const char *id, char *text, size_t size)
{
    char path[160];
    int status;

    snprintf(path, sizeof path, "/ingest/%s/down", id);
    status = http("POST", path, "", text, size);
    return status == 200 ? 1 : status == 204 ? 0 : -1;
}

/* Post one line over UDP, for a link with no TCP. Nothing comes back: no
   answer and no commands. Returns 0 when it left the board, -1 when not. */
int warblet_post_udp(const char *id, const char *line)
{
    int s, n;

    n = snprintf(buf, sizeof buf, "CHIRP1 %s - %s", id, line);   /* "-": no token */
    if (n < 0 || n >= (int)sizeof buf)
        return -1;
    s = open_socket(WARBLET_UDP_HOST, "7701", SOCK_DGRAM);
    if (s < 0)
        return -1;
    n = send(s, buf, n, 0) == n ? 0 : -1;
    close_socket(s);
    return n;
}

/* The whole starter: collect a command if one is waiting, then post
   "<count> <what it last heard>". Fills line; returns the post's status. */
int warblet_open_step(const char *id, char *line, size_t size)
{
    static char heard[1025] = "hello", command[1025];
    static long count = 0;

    if (warblet_get_command(id, command, sizeof command) == 1)
        strcpy(heard, command);                   /* say it back */
    snprintf(line, size, "%ld %s", ++count, heard);
    return warblet_post(id, line);
}

#ifdef WARBLET_MAIN   /* on a computer: cc -DWARBLET_MAIN warblet_open.c -o open */
int main(int argc, char **argv)
{
    char line[1040];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <board-id>\n", argv[0]);
        return 2;
    }
    for (;;) {
        int status = warblet_open_step(argv[1], line, sizeof line);
        printf("%s -> %d\n", line, status);
        fflush(stdout);
        sleep(10);
    }
}
#endif

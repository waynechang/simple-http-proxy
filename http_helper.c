#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "proxy.h"
#include "http.h"
#include "http_helper.h"
#include "iowrap.h"
#include "slog.h"

char *strcasestr(const char *haystack, const char *needle)
{
        const char *p, *match, *loc = NULL;
        if (!needle || !haystack) {
                return NULL;
        }

        match = needle;
        p = haystack;
        loc = NULL;

        while (*p != '\0' && *match != '\0') {
                if (tolower(*p) == tolower(*match)) {
                        if (!loc)
                                loc = p;
                        match++;
                } else {
                        loc = NULL;
                        match = needle;
                }
                p++;
        }

        if (*match == '\0') {
                return (char *)loc;
        }
        return NULL;
}

/* strip off beginning crlf if exists */
int purge_crlf(int fd)
{
        ssize_t n;
        char buf[8];
        n = recv(fd, buf, 2, MSG_PEEK);
        if (n == -1) {
                slog_perror("read");
                return -1; 
        } else if (n < 2) {
                slog(SLOG_ERROR, "purge_crlf: not enough characters");
                return -1;
        }

        if (buf[0] == '\r' && buf[1] == '\n') {
                n = readn(fd, buf, 2);
                if (n == -1) {
                        slog(SLOG_ERROR, "purge_crlf: chars not removed");
                        return -1;
                }
        }
        return 0;
}

int extract_method(const char *buf, int *method)
{
        if (buf == strcasestr(buf, "GET")) {
                *method = HTTP_GET;
        } else if (buf == strcasestr(buf, "POST")) {
                *method = HTTP_POST;
        } else if (buf == strcasestr(buf, "CONNECT")) {
                *method = HTTP_CONNECT;
        } else {
                *method = HTTP_UNKNOWN;
        }
        return 0;
}


int extract_encoding(const char *buf, int *chunked, size_t *clen)
{
        const char *loc, *brk;
        int cl;
        int c;
        brk = strcasestr(buf, "\r\n\r\n");
        if (!brk) {
                slog(SLOG_ERROR, "peek_host: malformed header!");
                return -1;
        }

        loc = strcasestr(buf, "Transfer-Encoding: chunked");
        if (loc != NULL && loc < brk) {
                *chunked = 1;
                *clen = 0;
                return 0;
        }

        loc = strcasestr(buf, "Content-Length:");
        if (loc != NULL && loc < brk) {
                /* @todo case insensitive sscanf */
                c = sscanf(loc, "Content-Length: %d\r\n", &cl);
                if (c == 0) {
                        slog(SLOG_ERROR, "content-length not found");
                        return -1;
                }
                slog(SLOG_DEBUG, "content length scanned %d; cl = %d", c, cl);

                *chunked = 0;
                *clen = cl;
                return 0;
        }

        slog(SLOG_DEBUG, "content length not found. setting to 0");
        *chunked = 0;
        *clen = 0;
        return 0;
}


int resolve_host(const char *hostname, struct in_addr *sin_addr)
{
        struct addrinfo hints, *result, *rp;
        struct sockaddr_in *addr;
        int s;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; /* ipv4 */
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE; /* for wildcard IP address */

        slog(SLOG_DEBUG, "%s", hostname);
        s = getaddrinfo(hostname, NULL, &hints, &result);
        if (s != 0) {
                slog(SLOG_ERROR, "getaddrinfo: %s", gai_strerror(s));
                return -1;
        }

        rp = result;
        if (rp == NULL) {
                slog(SLOG_ERROR, "resolve_host: could not resolve %s",
                     hostname);
                freeaddrinfo(result);
                return -1;
        }

        addr = (struct sockaddr_in *)rp->ai_addr;

        *sin_addr = addr->sin_addr;

        freeaddrinfo(result);
        return 0;
}



int extract_host(const char *buf, struct in_addr *sin_addr, in_port_t *sin_port) {
        char host_s[MAX_HOST_LEN];
        const char *hloc;
        int c, r;
        in_port_t tp = 0, p = 0;

        hloc = strcasestr(buf, "Host:");
        if (!hloc) {
                slog(SLOG_ERROR, "no host line");
                return -1;
        }

        /* @todo case insensitive sscanf */
        c = sscanf(hloc, "Host: %128[^:\r]:%hu\r", host_s, &tp);
        if (c == 2) {
                p = tp;
                slog(SLOG_DEBUG, "port from HOST = %hu", p);
        } else if (tp == 0) {
                slog(SLOG_DEBUG, "defaulting to port 80");
                p = 80;
        } else {
                slog(SLOG_ERROR, "nothing scanned");
                return -1;
        }
        slog(SLOG_DEBUG, "scanned %d\nhost: %s\nport: %d", c, host_s, p);

        *sin_port = htons(p);
        r = resolve_host(host_s, sin_addr);
        if (r == -1) {
                slog(SLOG_ERROR, "resolve_host() failed");
                return -1;
        }

        return 0;
}

int extract_uri_host(const char *buf,
                     struct in_addr *sin_addr, in_port_t *sin_port) {
        char host_s[MAX_HOST_LEN];
        char method[32];
        int c, r;
        in_port_t tp = 0, p = 0;

        c = sscanf(buf, "%31s %128[^: ]:%hu\r", method, host_s, &tp);
        if (c == 3) {
                p = tp;
                slog(SLOG_DEBUG, "port from uri = %hu", p);
        } else if (c == 2) {
                p = 80;
                slog(SLOG_DEBUG, "port defaulted to 80");
        } else {
                slog(SLOG_ERROR, "sscanf() < 2 read");
                return -1;
        }
        slog(SLOG_DEBUG, "scanned %d; host: %s; port: %d", c, host_s, p);

        *sin_port = htons(p);
        r = resolve_host(host_s, sin_addr);
        if (r == -1) {
                slog(SLOG_ERROR, "resolve_host() failed");
                return -1;
        }

        return 0;
}

int extract_uri(const char *buf, char *uri, size_t uri_len) {
        size_t s, e, n, i;

        for (s = 0; buf[s] != ' ' && buf[s] != '\0'; s++)
                ;

        if (buf[s] == '\0') {
                slog(SLOG_ERROR, "could not locate uri\n"
                                 "== uri error header ==\n"
                                 "%s\n"
                                 "== end uri error header ==", buf);
                return -1;
        }

        s++;

        for (e = s; buf[e] != ' ' && buf[e] != '\0'; e++)
                ;

        if (buf[e] == '\0') {
                slog(SLOG_ERROR, "could not locate uri 2nd space\n"
                                 "== uri error header ==\n"
                                 "%s\n"
                                 "== end uri error header ==", buf);
                return -1;
        }

        n = e - s;
        if (n < 0 || n >= uri_len) {
                slog(SLOG_ERROR, "bad uri character index or too long");
                return -1;
        }

        i = 0;
        while (i < n)
                uri[i++] = buf[s++];
        uri[i] = '\0';

        slog(SLOG_DEBUG, "uri: %s", uri);
        return 0;
}

int transfer_chunklen(int srcfd, int destfd, size_t *len)
{
        ssize_t n;
        size_t cnt;
        char buf[MAX_BUF+1];
        char *s;
        n = recv(srcfd, buf, MAX_BUF, MSG_PEEK);
        if (n == -1) {
                slog_perror("recv");
                return -1;
        }
        buf[n] = '\0';
        slog(SLOG_DEBUG, "transfer_chunklen() peeked %d bytes", (int) n);

        s = strcasestr(buf, "\r\n");
        if (!s) {
                slog(SLOG_ERROR, "unproperty-terminated chunking");
                return -1;
        }
        *s = '\0';
        s += 2;
        cnt = s - buf;

        *len = strtol(buf, NULL, 16);
        
        buf[cnt] = '\0';
        slog(SLOG_DEBUG, "\n==length data (%d bytes)==\n"
                         "%s\n"
                         "==end length data==", cnt, buf);


        slog(SLOG_DEBUG, "transfering %d bytes of length data", (int) cnt);
        n = transfern(srcfd, destfd, cnt);
        if (n == -1) {
                slog(SLOG_ERROR, "transfern() failed");
                return -1;
        }
        return 0;
}

int transfer_chunked(int srcfd, int destfd)
{
        int r;
        size_t len, total = 0;
        ssize_t n;
        const char *terminate;

        for (;;) {
                r = transfer_chunklen(srcfd, destfd, &len);
                if (r == -1) {
                        slog(SLOG_ERROR, "transfer_chunklen() failed"
                                         " exiting gracefully");
                        len = 0;
                        terminate = "0\r\n\r\n";
                        n = writen(destfd, terminate, strlen(terminate));
                        if (n == -1) {
                                slog(SLOG_ERROR, "graceful exit failed");
                        }
                        return -1;
                }

                if (len == 0) {
                        slog(SLOG_DEBUG, "chunk of length 0: exiting");
                        n = transfern(srcfd, destfd, 2);
                        if (n == -1) {
                                slog(SLOG_ERROR,
                                     "transfern() of last crlf failed");
                                return -1;
                        }
                        break;
                }

                len += 2; /* crlf */
                slog(SLOG_DEBUG, "transfering chunk of size %d...",
                               (int) len);
                n = transfern(srcfd, destfd, len);
                if (n == -1) {
                        slog(SLOG_ERROR, "transfern() failed");
                        return -1;
                }
                slog(SLOG_DEBUG, "done!");
                total += n;
        }

        slog(SLOG_DEBUG, "transfer_chunked() finished %d bytes", (int) total);


        return 0;
}


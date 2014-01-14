#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "proxy.h"
#include "iowrap.h"
#include "http.h"
#include "proxy.h"
#include "http_helper.h"
#include "http_method.h"
#include "slog.h"

int peek_header(int fd, char *buf, size_t cnt, size_t *hlen)
{
        int r;
        char *brk;
        ssize_t nr;

        r = waitread(fd);
        if (r == -1) {
                slog(SLOG_ERROR, "waitread() failed");
                return -1;
        } else if (r == 0) {
                slog(SLOG_DEBUG, "waitread() timed out");
                return -1;
        }

        nr = recv(fd, buf, cnt - 1, MSG_PEEK);
        if (nr == -1) {
                perror("recv");
                return -1;
        }
        buf[nr] = '\0';
        slog(SLOG_DEBUG, "peeked %d bytes", (int) nr);

        if ((brk = strstr(buf, "\r\n\r\n"))) {
                brk += 4;
                slog(SLOG_DEBUG, "end break found %d chars in!",
                     (int) (brk - buf));
        } else {
                slog(SLOG_ERROR, "peek_header: could not find break");
                return -1;
        }
        *brk = '\0';

        if (hlen)
                *hlen = brk - buf;
 
        return 0;
}

int transfer_header_body(int srcfd, int destfd)
{
        char buf[MAX_HEADER+1];
        int chunked, r;
        size_t clen, hlen;
        ssize_t n;

        r = peek_header(srcfd, buf, MAX_HEADER+1, &hlen);
        if (r == -1) {
                slog(SLOG_ERROR, "peek_header() failed");
                return -1;
        }

        slog(SLOG_DEBUG, "\n==src header==\n"
                         "%s\n"
                         "==end src header==", buf);

        r = extract_encoding(buf, &chunked, &clen);
        if (r == -1) {
                slog(SLOG_ERROR, "could not read encoding");
                return -1;
        }
        slog(SLOG_DEBUG, "Chunked: %s", chunked ? "Yes" : "No");
        slog(SLOG_DEBUG, "Content Length: %d", (int) clen);

        n = transfern(srcfd, destfd, hlen);
        if (n == -1) {
                slog(SLOG_ERROR, "transfern() of header failed");
                return -1;
        }

        if (!chunked) {
                n = transfern(srcfd, destfd, clen);
                if (n == -1) {
                        slog(SLOG_ERROR, "transfern() of content failed\n");
                        return -1;
                }
                slog(SLOG_DEBUG, "transfered %d bytes of content\n",
                     (int) n);
        } else {
                r = transfer_chunked(srcfd, destfd);
                if (r == -1) {
                        slog(SLOG_ERROR, "transfer_chunked() failed");
                        return -1;
                }
        }
        return 0;
}



#define peek_uri_host(x, y) _peek_host(x, y, extract_uri_host)
#define peek_host(x, y) _peek_host(x, y, extract_host)

int _peek_host(int fd, struct sockaddr_in *dest,
               int  (extractor(const char *buf,
                               struct in_addr *sin_addr,
                               in_port_t *sin_port)))
{ int r;
        size_t hlen;
        char buf[MAX_HEADER+1];
        char ip_s[16];

        r = peek_header(fd, buf, MAX_HEADER+1, &hlen);
        if (r == -1) {
                slog(SLOG_ERROR, "peek_header() failed");
                return -1;
        }

        memset(dest, 0, sizeof(dest));
        dest->sin_family = AF_INET;
        r = extractor(buf, &dest->sin_addr, &dest->sin_port);
        if (r == -1) {
                slog(SLOG_ERROR, "extract_host() failed");
                return -1;
        }
        inet_ntop(AF_INET, &dest->sin_addr, ip_s, 16);
        slog(SLOG_DEBUG, "resolved host to %s", ip_s);
        return 0;
}



/*
 * @todo handle persistent connections
 */
int handle_get(int clientfd)
{
        int r, rv = 0;
        int destfd;
        struct sockaddr_in dest;

        slog(SLOG_DEBUG, "\nPARSE HOST DATA");
        r = peek_host(clientfd, &dest);
        if (r == -1) {
                slog(SLOG_ERROR, "peek_host() to server failed");
                return -1;
        }

        destfd = socket(AF_INET, SOCK_STREAM, 0);
        if (destfd == -1) {
                slog_perror("socket");
                return -1;
        }

        slog(SLOG_DEBUG, "connecting...");
        r = connect(destfd, (struct sockaddr *)&dest, sizeof(dest));
        if (r == -1) {
                slog_perror("connect");
                rv = -1;
                goto cleanup;
        }

        slog(SLOG_DEBUG, "\nSEND CLIENT REQUEST");
        r = transfer_header_body(clientfd, destfd);
        if (r == -1) {
                slog(SLOG_ERROR, "transfer_header_body() to server failed");
                rv = -1;
                goto cleanup;
        }

        slog(SLOG_DEBUG, "\nRECV SERVER RESPONSE");
        r = transfer_header_body(destfd, clientfd);
        if (r == -1) {
                slog(SLOG_ERROR, "transfer_header_body() to client failed");
                rv = -1;
                goto cleanup;
        }

cleanup:
        slog(SLOG_DEBUG, "closing destfd...");
        r = close(destfd);
        if (r == -1) {
                slog_perror("close");
                rv = -1;
        }

        slog(SLOG_DEBUG, "--\n");

        return rv;
}

int burn_header(int fd)
{
        int r;
        char buf[MAX_BUF+1];
        size_t hlen;
        ssize_t n;
        r = peek_header(fd, buf, MAX_BUF, &hlen);
        if (r == -1) {
                slog(SLOG_ERROR, "peek_header() failed");
                return -1;
        }

        n = readn(fd, buf, hlen);
        if (n == -1) {
                slog(SLOG_ERROR, "readn() failed");
                return -1;
        }

        return 0;
}

int handle_connect(int clientfd)
{
        int r, rv = 0;
        int destfd;
        struct sockaddr_in dest;
        const char *resp;
        ssize_t n;

        slog(SLOG_DEBUG, "\nCONNECT PARSE HOST DATA");
        r = peek_uri_host(clientfd, &dest);
        if (r == -1) {
                slog(SLOG_ERROR, "peek_host() to server failed");
                return -1;
        }

        destfd = socket(AF_INET, SOCK_STREAM, 0);
        if (destfd == -1) {
                slog_perror("socket");
                return -1;
        }

        slog(SLOG_DEBUG, "connecting...");
        r = connect(destfd, (struct sockaddr *)&dest, sizeof(dest));
        if (r == -1) {
                slog_perror("connect");
                rv = -1;
                goto cleanup;
        }

        r = burn_header(clientfd);
        if (r == -1) {
                slog(SLOG_ERROR, "burn_header() failed");
                rv = -1;
                goto cleanup;
        }

        resp = "HTTP/1.1 200 Connection established\r\n"
               "Proxy-agent: SimpleProxy/1.1\r\n"
               "\r\n";
        n = writen(clientfd, resp, strlen(resp));
        if (n == -1) {
                slog(SLOG_DEBUG, "writen() failed");
                rv = -1;
                goto cleanup;
        }

        r = fdtunnel(clientfd, destfd);
        if (r == -1) {
                slog(SLOG_ERROR, "connect_tunnel() failed");
                rv = -1;
                goto cleanup;
        }
        

cleanup:
        slog(SLOG_DEBUG, "closing destfd...");
        r = close(destfd);
        if (r == -1) {
                slog_perror("close");
                rv = -1;
        }

        slog(SLOG_DEBUG, "--\n");

        return rv;
}


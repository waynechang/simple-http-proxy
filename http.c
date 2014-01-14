#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>
#include "proxy.h"
#include "iowrap.h"
#include "http.h"
#include "http_helper.h"
#include "http_method.h"
#include "slog.h"

void http_log(struct in_addr *sin_addr, char *uri, const char* pathname)
{
        char log_s[MAX_BUF];
        char time_str[64];
        char ip_s[16];
        const char *s;
        time_t now;
        FILE *fp;
        int r;
        size_t n, len;

        now = time(NULL);
        strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

        s = inet_ntop(AF_INET, sin_addr, ip_s, sizeof(ip_s));
        if (!s) {
                slog_perror("inet_ntop");
                return;
        }

        snprintf(log_s, MAX_BUF, "%s: %s %s\n", time_str, ip_s, uri);
        len = strlen(log_s);

        fp = fopen(pathname, "a");
        if (!fp) {
                slog_perror("fopen");
                return;
        }

        n = fwrite(log_s, 1, len, fp);
        if (n != len) {
                slog(SLOG_ERROR, "fwrite: %d written out of %d", n, len);
        }

        r = fclose(fp);
        if (r == -1) {
                slog_perror("fclose");
        }

}

int peek_uri(int clientfd, char *uri, size_t uri_len)
{
        char buf[1024];
        ssize_t n;
        int r;
        n = recv(clientfd, buf, sizeof(buf) - 1, MSG_PEEK);
        if (n == -1) {
                slog_perror("recv");
                return -1;
        }
        buf[n] = '\0';

        r = extract_uri(buf, uri, uri_len);
        if (r == -1) {
                slog(SLOG_ERROR, "extract_uri() failed");
                return -1;
        }

        return 0;
}

int handle_http(int clientfd)
{
        char buf[32];
        ssize_t n;
        int method;
        int r = 0;

        n = recv(clientfd, buf, sizeof(buf) - 1, MSG_PEEK);
        if (n == -1) {
                slog_perror("recv");
                return -1;
        }
        buf[n] = '\0';
        r = extract_method(buf, &method);
        if (r == -1) {
                slog(SLOG_ERROR, "extract_method() failed");
                return -1;
        }

        switch(method) {
        case HTTP_GET:
                slog(SLOG_DEBUG, "handing GET request!");
                r = handle_get(clientfd);
                if (r == -1) {
                        slog(SLOG_ERROR, "handle_get() failed");
                        return -1;
                }
                break;
        case HTTP_POST:
                slog(SLOG_DEBUG, "handling POST request!");
                /* GET/POST can be handled the same */
                r = handle_get(clientfd);
                if (r == -1) {
                        slog(SLOG_ERROR, "handle_get() failed (for POST)");
                        return -1;
                }
                break;
        case HTTP_CONNECT:
                slog(SLOG_DEBUG, "handling CONNECT request!");
                r = handle_connect(clientfd);
                if (r == -1) {
                        slog(SLOG_ERROR, "handle_connect()");
                        return -1;
                }
                break;
        default:
                slog(SLOG_ERROR, "method not supported!\n");
                break;
        }

        slog(SLOG_DEBUG, "handle_http done!");
        return 0;
}


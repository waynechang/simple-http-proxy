#ifndef _HTTP_H
#define _HTTP_H

#define MAX_HEADER      8192
#define MAX_BUF         8192
#define MAX_HOST_LEN    64
#define MAX_PORT_LEN    8
#define MAX_METHOD_BUF  32

#define HTTP_UNKNOWN    (-1)
#define HTTP_OPTIONS    0
#define HTTP_GET        1
#define HTTP_HEAD       2
#define HTTP_POST       3
#define HTTP_PUT        4
#define HTTP_DELETE     5
#define HTTP_TRACE      6
#define HTTP_CONNECT    7

void http_log(struct in_addr *sin_addr, char *uri, const char *pathname);
int peek_uri(int clientfd, char *uri, size_t uri_len);
int handle_http(int listenfd);

#endif

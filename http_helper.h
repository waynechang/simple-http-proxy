#ifndef _HTTP_HELPER_H
#define _HTTP_HELPER_H

#include <netinet/in.h>
#include "http.h"

int purge_crlf(int fd);

int extract_method(const char *buf, int *method);
int extract_encoding(const char *buf, int *chunked, size_t *content_length);

int resolve_host(const char *hostname, struct in_addr *sin_addr);
int extract_host(const char *buf,
                 struct in_addr *sin_addr, in_port_t *sin_port);
int extract_uri_host(const char *buf,
                     struct in_addr *sin_addr, in_port_t *sin_port);

int extract_uri(const char *buf, char *uri, size_t uri_len);

int transfer_chunked(int srcfd, int destfd);


#endif

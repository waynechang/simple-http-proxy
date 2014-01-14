#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "iowrap.h"
#include "slog.h"

ssize_t readn(int fd, void *buf, size_t cnt)
{
        ssize_t n;
        size_t nr = 0;
        char *cbuf = buf;
        while (nr < cnt) {
                n = read(fd, &cbuf[nr], cnt - nr);
                if (n == -1) {
                        slog_perror("readn");
                        return n;
                }
                nr += n;
        }

        return cnt;
}

ssize_t writen(int fd, const void *buf, size_t cnt)
{
        ssize_t n;
        size_t nw = 0;
        const char *cbuf = buf;
        while (nw < cnt) {
                n = write(fd, &cbuf[nw], cnt - nw);
                if (n == -1) {
                        slog_perror("writen");
                        return n;
                }
                nw += n;
        }
        
        return cnt;
}

ssize_t transfern(int srcfd, int destfd, size_t cnt)
{
        ssize_t n;
        size_t nt, tleft = cnt;
        char buf[MAX_BUF];

        while (tleft > 0) {
                nt = tleft % MAX_BUF;
                nt = (nt == 0) ? MAX_BUF : nt;
                
                n = readn(srcfd, buf, nt);
                if (n == -1) {
                        fprintf(stderr, "transfern: readn() failed\n");
                        return -1;
                }

                n = writen(destfd, buf, nt);
                if (n == -1) {
                        fprintf(stderr, "transfern: writen() failed\n");
                        return -1;
                }

                tleft -= nt;
        }

        return cnt;
}

int waitready(int fd, int type)
{
        int r;
        fd_set set, *rset, *wset;
        struct timeval timeout;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;

        if (type == IOWRAP_READ) {
                rset = &set;
                wset = NULL;

        } else {
                rset = NULL;
                wset = &set;
        }


        r = select(fd + 1, rset, wset, NULL, &timeout);
        if (r == -1) {
                slog_perror("select");
        }
        return r;
}

int fdtunnel(int srcfd, int destfd)
{
        int r, maxfd;
        fd_set rset;
        char buf[MAX_BUF];
        ssize_t nr, nw;
        struct timeval timeout;

        for (;;) {
                FD_ZERO(&rset);
                FD_SET(srcfd, &rset);
                FD_SET(destfd, &rset);
                timeout.tv_sec = 30;
                timeout.tv_usec = 0;
                maxfd = (srcfd > destfd ? srcfd : destfd) + 1;
                
                slog(SLOG_DEBUG, "selecting...");
                r = select(maxfd, &rset, NULL, NULL, &timeout);
                if (r == -1) {
                        slog_perror("select");
                        break;
                } else if (r == 0) {
                        slog(SLOG_DEBUG, "timeout elapsed. leaving...");
                        break;
                }

                if (FD_ISSET(srcfd, &rset)) {
                        slog(SLOG_DEBUG, "transfering src to dest...");
                        nr = read(srcfd, buf, MAX_BUF);
                        if (nr == -1) {
                                slog_perror("read");
                                return -1;
                        } else if (nr == 0) {
                                slog(SLOG_DEBUG, "socket closed");
                                break;
                        }
                        nw = writen(destfd, buf, nr);
                        if (nw == -1) {
                                slog(SLOG_ERROR, "writen() failed");
                                return -1;
                        }
                        slog(SLOG_DEBUG, "wrote %d bytes src to dest", nw);
                }

                if (FD_ISSET(destfd, &rset)) {
                        slog(SLOG_DEBUG, "transfering dest to src...");
                        nr = read(destfd, buf, MAX_BUF);
                        if (nr == -1) {
                                slog_perror("read");
                                return -1;
                        } else if (nr == 0) {
                                slog(SLOG_DEBUG, "socket closed");
                                break;
                        }
                        nw = writen(srcfd, buf, nr);
                        if (nw == -1) {
                                slog(SLOG_ERROR, "writen() failed");
                                return -1;
                        }
                        slog(SLOG_DEBUG, "wrote %d bytes dest to src", nw);
                }
        }
        return 0;
}


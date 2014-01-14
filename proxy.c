/*
 * proxy.c - A Simple Concurrent Web proxy
 *
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include "proxy.h"
#include "http.h"
#include "launch.h"
#include "slog.h"
#include "shm.h"

/* main routine */
int main(int argc, char **argv)
{
        int listenfd, r;
        unsigned short port;
        struct sockaddr_in serv;
        int mode;
        int *log_level;

        signal(SIGPIPE, SIG_IGN);
        log_level = shmalloc("/log_level", sizeof(int));
        if (!log_level) {
                slog(SLOG_ERROR, "could not allocate shm for slog");
                exit(EXIT_FAILURE);
        }
        *log_level = SLOG_ALL;
        slog_level(log_level);

        if (argc < 2) {
                fprintf(stderr, "Usage: %s <port number> "
                                "[mode: 0=seq,1=thr,2=proc]\n", argv[0]);
                exit(EXIT_FAILURE);
        } else if (argc >= 3) {
                mode = atoi(argv[2]);
                if (argc >= 4) {
                        *log_level = atoi(argv[3]);
                        fprintf(stderr, "%s: *log_level = %d\n",
                                argv[0], *log_level);
                }

        } else {
                fprintf(stderr, "%s: no mode specified. "
                                "defaulting to mode=0 (seq)\n", argv[0]);
                mode = 0;
        }

        if ((port = atoi(argv[1])) <= 0) {
                fprintf(stderr, "%s: bad port\n", argv[0]); 
                exit(EXIT_FAILURE);
        }

        listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd == -1) {
                slog_perror("socket");
                exit(EXIT_FAILURE);
        }


        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = htonl(INADDR_ANY);
        serv.sin_port = htons(port);

        r = bind(listenfd, (struct sockaddr *)&serv, sizeof(serv));
        if (r == -1) {
                slog_perror("bind");
                exit(EXIT_FAILURE);
        }

        r = listen(listenfd, LISTENQ);
        if (r == -1) {
                slog_perror("listen");
                exit(EXIT_FAILURE);
        }

        if (mode == 0) {
                proxy_seq(listenfd);
        } else if (mode == 1) {
                proxy_thr(listenfd);
        } else {
                proxy_proc(listenfd);
        }

        r = close(listenfd);
        if (r == -1) {
                slog_perror("close"); 
                exit(EXIT_FAILURE);
        }

        shfree("/log_level", log_level, sizeof(int));
        exit(EXIT_SUCCESS);
}


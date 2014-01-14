#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

#include "shm.h"
#include "proxy.h"
#include "http.h"
#include "launch.h"
#include "slog.h"

int accept_client(int listenfd, struct sockaddr_in *client)
{
        socklen_t len;
        int clientfd;
        char ip_s[16];

        len = sizeof(*client);
        clientfd = accept(listenfd, (struct sockaddr *)client, &len);
        if (clientfd == -1) {
                slog_perror("accept");
                return -1;
        }

        slog(SLOG_DEBUG, "clientfd = %d", clientfd);
        slog(SLOG_DEBUG, "Connection from %s:%d",
             inet_ntop(AF_INET, &client->sin_addr, ip_s,
                       sizeof(ip_s)), ntohs(client->sin_port));

        return clientfd;
}

void thr_cleanup(void *data)
{
        int r;
        thread_data *td = data;
        slog(SLOG_DEBUG, "thread closing clientfd = %d", td->clientfd);
        r = close(td->clientfd);
        if (r == -1) {
                slog_perror("close");
        }
        pthread_mutex_unlock(td->log_lock);

        free(td);
}

void *thr_handle_http(void *data)
{
        
        int r;
        thread_data *td;
        int clientfd;
        char uri[MAX_HEADER+1];

        td = data;
        clientfd = td->clientfd;

        slog(SLOG_DEBUG, "thread clientfd = %d", clientfd);

        pthread_cleanup_push(thr_cleanup, td);

        r = peek_uri(clientfd, uri, MAX_HEADER);
        if (r == -1) {
                slog(SLOG_ERROR, "could not find uri");
                *uri = '\0';
        }

        r = handle_http(clientfd);
        if (r == -1) {
                slog(SLOG_ERROR, "handle_http failed");
        }

        while(pthread_mutex_lock(td->log_lock))
                slog_perror("pthread_mutex_lock");

        http_log(&td->client.sin_addr, uri, "proxy.log");

        while(pthread_mutex_unlock(td->log_lock))
                slog_perror("pthread_mutex_unlock");

        pthread_cleanup_pop(!0);

        pthread_exit(0);
}

int proxy_thr(int listenfd)
{
        int clientfd;
        thread_data *td;
        pthread_mutex_t log_lock;
        slog(SLOG_DEBUG, "running proxy_thr()");
        
        pthread_mutex_init(&log_lock, NULL);

        for (;;) {
                /* td freed in thr_cleanup() */
                td = malloc(sizeof(thread_data));
                if (!td) {
                        slog_perror("malloc");
                        continue;
                }

                clientfd = accept_client(listenfd, &td->client);
                if (clientfd == -1) {
                        slog(SLOG_ERROR, "bad client connection");
                        free(td);
                        continue;
                }
                
                td->log_lock = &log_lock;
                td->clientfd = clientfd;
                pthread_create(&td->thread, NULL, thr_handle_http, td);
        }

        pthread_mutex_destroy(&log_lock);
        return 0;
}

int proc_work(int clientfd, struct sockaddr_in *client, sem_t *log_sem)
{
        int r;
        char uri[MAX_HEADER+1];

        r = peek_uri(clientfd, uri, MAX_HEADER);
        if (r == -1) {
                slog(SLOG_ERROR, "could not find uri");
                *uri = '\0';
        }

        r = handle_http(clientfd);
        if (r == -1) {
                slog(SLOG_ERROR, "handle_http failed");
        }

        slog(SLOG_DEBUG, "writing to http log...");

        while (sem_wait(log_sem))
                slog_perror("sem_wait");

        http_log(&client->sin_addr, uri, "proxy.log");

        while (sem_post(log_sem))
                slog_perror("sem_post");

        slog(SLOG_DEBUG, "closing clientfd...");
        r = close(clientfd);
        if (r == -1) {
                slog_perror("close");
                return -1;
        }

        return 0;
 
}

int proxy_proc(int listenfd)
{
        struct sockaddr_in client;
        int clientfd, r;
        pid_t pid, wpid;
        sem_t *sem;
        int status, i, reaped;
        int *dead_count;
        slog(SLOG_DEBUG, "running proxy_proc()");

        dead_count = shmalloc("/dead_count", sizeof(int));
        if (!dead_count) {
                slog(SLOG_ERROR, "could not allocate shared process count");
                return -1;
        }
        *dead_count = 0;

        sem = shmalloc("/log_lock", sizeof(sem_t));
        if (!sem) {
                slog(SLOG_ERROR, "could not allocate shared log lock");
                return -1;
        }

        r = sem_init(sem, !0, 1); /* init to 1 */
        if (r == -1) {
                slog(SLOG_ERROR, "log lock failed to initialize");
                return -1;
        }

        for (;;) {
                clientfd = accept_client(listenfd, &client);
                if (clientfd == -1) {
                        slog(SLOG_ERROR, "bad client connection");
                        continue;
                }
                
                pid = fork();
                if (pid == 0) {
                        r = proc_work(clientfd, &client, sem);
                        if (r == -1) {
                                slog(SLOG_ERROR, "proc_work() failed");
                                dead_count++;
                                exit(EXIT_FAILURE);
                        }
                        slog(SLOG_ERROR, "proc_work() exited normally");
                        dead_count++;
                        exit(EXIT_SUCCESS);
                }

                for (i = 0, reaped = 0; i < *dead_count; i++) {
                        wpid = waitpid(-1, &status, WNOHANG);
                        if (wpid == -1) {
                                slog_perror("waitpid");
                        } else if (wpid != 0) {
                                slog(SLOG_DEBUG, "pid = %d reaped.", wpid);
                                reaped++;
                        }
                }
                *dead_count -= reaped;
        }

        r = sem_destroy(sem);
        if (r == -1) {
                slog(SLOG_ERROR, "failed to destroy log lock");
                return -1;
        }

        shfree("/log_locl", sem, sizeof(sem_t));
        shfree("/dead_count", dead_count, sizeof(int));
        return 0;
}

int proxy_seq(int listenfd)
{
        struct sockaddr_in client;
        int clientfd, r;
        char uri[MAX_HEADER+1];
        slog(SLOG_DEBUG, "running proxy_seq()");

        for (;;) {
                slog(SLOG_DEBUG, "accepting a client...");
                clientfd = accept_client(listenfd, &client);
                if (clientfd == -1) {
                        slog(SLOG_ERROR, "bad client connection");
                        continue;
                }

                r = peek_uri(clientfd, uri, MAX_HEADER);
                if (r == -1) {
                        slog(SLOG_ERROR, "could not find uri");
                        *uri = '\0';
                }

                r = handle_http(clientfd);
                if (r == -1) {
                        slog(SLOG_ERROR, "handle_http failed");
                }

                slog(SLOG_DEBUG, "writing to http log...");
                http_log(&client.sin_addr, uri, "proxy.log");

                slog(SLOG_DEBUG, "closing clientfd...");
                r = close(clientfd);
                if (r == -1) {
                        slog_perror("close");
                }
        }

        return 0;
}


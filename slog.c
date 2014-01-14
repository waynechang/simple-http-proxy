#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "slog.h"


#define LEVEL_GUARD(level) \
        if (_slog_level && level > *_slog_level) \
                return;

static int *_slog_level = 0;

void slog_level(int *level)
{
        _slog_level = level;
}

const char *lts(int level)
{
        const char* s;
        switch (level) {
        case SLOG_DEBUG:
                s = "DEBUG";
                break;
        case SLOG_WARN:
                s = "WARN";
                break;
        case SLOG_ERROR:
                s = "ERROR";
                break;
        default:
                s = NULL;
                break;
        }
        return s;
}

void slogs(int level, FILE *stream, const char *format, va_list args)
{
        struct timeval tv;
        char buf[SLOG_BUF];

        LEVEL_GUARD(level);

        gettimeofday(&tv, NULL); 
        strftime(buf, SLOG_BUF, "%Y-%m-%d %H%M%S", localtime(&tv.tv_sec));
        fprintf(stream, "[PID%7d][%5s][%s.%06ld] ",
                getpid(), lts(level), buf, tv.tv_usec);

        vfprintf(stream, format, args);

        fprintf(stream, "\n");
        fflush(stream);
}

void slogf(int level, const char *pathname, const char *format, ...)
{
        va_list args;
        int r;
        FILE *stream;

        LEVEL_GUARD(level);

        stream = fopen(pathname, "a");
        if (!stream) {
                perror("fopen");
                return;
        }

        va_start(args, format);
        slogs(level, stream, format, args);
        va_end(args);

        r = fclose(stream);
        if (r == -1) {
                perror("fclose");
        }
}

void slog(int level, const char *format, ...)
{
        va_list args;

        LEVEL_GUARD(level);

        va_start(args, format);
        slogs(level, stderr, format, args);
        va_end(args);
}

void slog_perror(const char *s)
{
        char msg[SLOG_BUF];
        char buf[SLOG_BUF];

        LEVEL_GUARD(SLOG_ERROR);

        strerror_r(errno, buf, SLOG_BUF);
        snprintf(msg, SLOG_BUF, "%s: %s", s, buf);
        slog(SLOG_ERROR, msg);
}


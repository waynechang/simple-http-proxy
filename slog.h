#ifndef _SLOG_H
#define _SLOG_H

#define SLOG_NONE       0
#define SLOG_ERROR      1
#define SLOG_WARN       2
#define SLOG_DEBUG      3
#define SLOG_ALL        4

#define SLOG_BUF         8192

void slog_level(int *level);
void slogf(int level, const char *pathname, const char *format, ...);
void slog(int level, const char *format, ...);
void slog_perror(const char *s);

#endif

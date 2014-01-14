#define MAXLINE 8192
#define LISTENQ 16

void write_log();

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size);

extern struct sockaddr_in g_sockaddr;
extern char g_logstring[1024];
extern char g_uri[512];
extern int g_size;

extern int g_debug;


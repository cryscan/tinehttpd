#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>

#define ISspace(x) isspace((int)x)

void error_die(const char*);

void error_die(const char* str)
{
	perror(str);
	exit(1);
}

int main(int argc, char* argv[])
{
	return 0;
}

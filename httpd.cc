#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <regex>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void error_die(const char*);

void recv_data(int fd, int events, void *arg);
void send_data(int fd, int events, void *arg);
void accept_connect(int fd, int events, void *arg);

struct event_tag
{
	int fd;
	void (*call_back)(int, int, void*);
	int events;
	void *arg;
	int status;
	char buf[256];
	int len;

	event_tag(int fd, void (*call_back)(int, int, void*), void *arg)
	{
		this->fd = fd;
		this->call_back = call_back;
		this->events = 0;
		this->arg = arg;
		this->status = 0;
	}

	void update(int epollfd, int events)
	{
		struct epoll_event ev = {0, {0}};
		int op;
		ev.data.ptr = this;
		ev.events = this->events = events;
		if(this->status == 1)
			op = EPOLL_CTL_MOD;
		else
		{
			op = EPOLL_CTL_ADD;
			this->status = 1;
		}
		if(epoll_ctl(epollfd, op, this->fd, &ev) < 0)
			error_die("epoll add failed.\n");
	}

	void remove(int epollfd)
	{
		struct epoll_event ev = {0, {0}};
		if(this->status != 1)
			return;
		ev.data.ptr = this;
		this->status = 0;
		epoll_ctl(epollfd, EPOLL_CTL_DEL, this->fd, &ev);
	}
};

void error_die(const char* str)
{
	perror(str);
	exit(1);
}

int main(int argc, char* argv[])
{
	return 0;
}

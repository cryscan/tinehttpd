#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
#include "event.h"

void error_die(const char *);
int http_proc(int, stringstream &, void *);

void event_tag::reset(int fd, void (*call_back) (int, int, void *), void *arg)
{
	this->fd = fd;
	this->call_back = call_back;
	this->events = 0;
	this->status = 0;
	this->proc = http_proc;
	arg ? this->arg = arg : this->arg = this;
	data.clear();
}

void event_tag::update(int epollfd, int events)
{
	struct epoll_event ev = { 0, {0} };
	int op;
	ev.data.ptr = this;
	ev.events = this->events = events;
	if (this->status == 1)
		op = EPOLL_CTL_MOD;
	else
	{
		op = EPOLL_CTL_ADD;
		this->status = 1;
	}
	if (epoll_ctl(epollfd, op, this->fd, &ev) < 0)
		error_die("epoll add failed.\n");
}

void event_tag::remove(int epollfd)
{
	struct epoll_event ev = { 0, {0} };
	if (this->status != 1)
		return;
	ev.data.ptr = this;
	this->status = 0;
	epoll_ctl(epollfd, EPOLL_CTL_DEL, this->fd, &ev);
}

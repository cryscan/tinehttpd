#include <ctype.h>
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

#define max_events 512
using namespace std;

void error_die(const char*);

void recv_data(int, int, void*);
void send_data(int, int, void*);
void accept_connect(int, int, void*);

struct event_tag
{
	int fd;
	void (*call_back)(int, int, void*);
	int events;
	void *arg;
	char buff[1024];
	int len;
	int status;

	void reset(int fd, void (*call_back)(int, int, void*), void *arg)
	{
		this->fd = fd;
		this->call_back = call_back;
		this->events = 0;
		this->status = 0;
		(int)arg ? this->arg = arg : this->arg = this;
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

int epollfd;
event_tag *event_tag_lst[max_events];

void error_die(const char* str)
{
	perror(str);
	exit(1);
}

void accept_connect(int fd, int events, void *arg)
{
	sockaddr_in client_name;
	unsigned int client_name_len = sizeof(client_name);

	int clientfd, i;
	if((clientfd = accept(fd, (sockaddr*)&client_name, &client_name_len)) == -1)
		if(errno != EAGAIN && errno != EINTR)
			return;

	do
	{
		if(fcntl(clientfd, F_SETFL, O_NONBLOCK) < 0)
			break ;
		for(i = 0; i < max_events; i++)
			if(event_tag_lst[i]->status == 0)
				break;
		if(i == max_events)
			break;

		event_tag_lst[i]->reset(clientfd, recv_data, (void*)0);
		event_tag_lst[i]->update(epollfd, EPOLLIN|EPOLLET);
	}
	while(0);
}

void recv_data(int fd, int events, void *arg)
{
	event_tag *ev = (event_tag*)arg;
	char buff[1024];
	int len = recv(fd, buff, sizeof(buff)-1, 0);

	ev->remove(epollfd);
	if(len > 0)
	{
		buff[len] = '\0';
		strncpy(ev->buff, buff, len);
		ev->len = len;

		ev->reset(fd, send_data, (void*)0);
		ev->update(epollfd, EPOLLOUT|EPOLLET);
	}
	else
		close(fd);
}

void send_data(int fd, int events, void *arg)
{
	event_tag *ev = (event_tag*)arg;
	int len = send(fd, ev->buff, ev->len, 0);
	ev->len = 0;

	ev->remove(epollfd);
	if(len > 0)
	{
		ev->reset(fd, recv_data, (void*)0);
		ev->update(epollfd, EPOLLIN|EPOLLET);
	}
	else
		close(fd);
}

int main(int argc, char* argv[])
{
	epollfd = epoll_create(32767);
	if(epollfd <= 0)
		error_die("epoll_create()\n");

	int i;
	for(i = 0; i < max_events; i++)
		event_tag_lst[i] = new event_tag();

	close(epollfd);
	return 0;
}

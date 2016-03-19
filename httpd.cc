#include <string>
#include <sstream>
#include <iostream>
#include <regex>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define max_events 512
using namespace std;

void error_die(const char*);
int startup(u_short*);
int read_content(char*, int);
void cat(int, FILE*);
void cannot_execute(int);
void execute(int, const char*,const char*);
void headers(int, int);
void not_found(int);
void serve_file(int, const char*);
void unimplemented(int);

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
	long last_active;

	void reset(int fd, void (*call_back)(int, int, void*), void *arg)
	{
		this->fd = fd;
		this->call_back = call_back;
		this->events = 0;
		this->status = 0;
		(int)arg ? this->arg = arg : this->arg = this;
		this->last_active = time(NULL);
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
event_tag *event_tag_lst[max_events + 1];

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
		strncpy(ev->buff, buff, len + 1);
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
	ev->len = read_content(ev->buff, ev->len);
	headers(fd, ev->len);
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

int read_content(char* buff, int len)
{
	int nlen = 0;
	istringstream strm(buff);
	string line;
	memset(buff, 0, len + 1);

	getline(strm, line);
	strcpy(&buff[nlen], line.c_str());
	nlen += line.size();
	buff[nlen] = '\0';

	return nlen;
}

void headers(int clientfd, int len)
{
	char buff[1024];

	strcpy(buff, "HTTP/1.1 200 OK\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Server:jdbhttpd/0.1.0\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Content-Type:text/html\r\n");
	send(clientfd, buff, strlen(buff), 0);
	sprintf(buff, "Content-Length:%d\r\n", len);
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "\r\n");
	send(clientfd, buff, strlen(buff), 0);
}

int startup(u_short* port)
{
	int httpd = socket(PF_INET, SOCK_STREAM, 0);
	sockaddr_in name;

	if(httpd == -1)
		error_die("httpd\n");
	fcntl(httpd, F_SETFL, O_NONBLOCK);
	event_tag_lst[max_events]->reset(httpd, accept_connect, (void*)0);
	event_tag_lst[max_events]->update(epollfd, EPOLLIN|EPOLLET);

	memset(&name, 0, sizeof(name));
	name.sin_family = AF_INET;
	name.sin_port = htons(*port);
	name.sin_addr.s_addr = htons(INADDR_ANY);

	if(bind(httpd, (sockaddr*)&name, sizeof(name)) < 0)
		error_die("bind\n");
	if(*port == 0)
	{
		unsigned int namelen = sizeof(name);
		if(getsockname(httpd, (sockaddr*)&name, &namelen) == -1)
			error_die("get socket name\n");
		*port = ntohs(name.sin_port);
	}
	if(listen(httpd, SOMAXCONN) < 0)
		error_die("listen\n");

	return httpd;
}

int main(int argc, char* argv[])
{
	epollfd = epoll_create(max_events + 1);
	if(epollfd <= 0)
		error_die("epoll_create()\n");

	int i;
	for(i = 0; i < max_events + 1; i++)
		event_tag_lst[i] = new event_tag();

	int sockfd = -1;
	u_short port = 0;

	sockfd = startup(&port);
	printf("httpd running on port %d\n", port);

	epoll_event events[max_events];
	while(true)
	{
		int numfd = epoll_wait(epollfd, events, max_events, -1);
		if(numfd < 0)
			error_die("wait\n");
		for(i = 0; i < numfd; i++)
		{
			event_tag *ev = (event_tag*)events[i].data.ptr;
			if((events[i].events & EPOLLIN) && (ev->events & EPOLLIN))
				ev->call_back(ev->fd, events[i].events, ev->arg);
			if((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
				ev->call_back(ev->fd, events[i].events, ev->arg);

		}
	}

	close(sockfd);
	close(epollfd);
	return 0;
}

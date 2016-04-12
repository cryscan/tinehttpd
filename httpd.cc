#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <map>
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
void unimplemented(int);

void headers(int, int);
int read_data(int, stringstream&);
int serve_file(int, const string);
int execute_cgi(int, const string, const string, const string, const map<string, string>&);
int upgrade_protocol(int, const map<string, string>&);
void not_found(int);
void bad_request(int);
void cannot_execute(int);

void recv_data(int, int, void*);
void send_data(int, int, void*);
void accept_connect(int, int, void*);

struct event_tag
{
	int fd;
	void (*call_back)(int, int, void*);
	int events;
	void *arg;
	int status;
	stringstream data;

	void reset(int fd, void (*call_back)(int, int, void*), void *arg)
	{
		this->fd = fd;
		this->call_back = call_back;
		this->events = 0;
		this->status = 0;
		arg ? this->arg = arg : this->arg = this;
		data.clear();
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
	printf("client:%s\n", inet_ntoa(client_name.sin_addr));

	do
	{
		if(fcntl(clientfd, F_SETFL, O_NONBLOCK) < 0)
			break;
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
	int len = 0;

	ev->data.clear();
	ev->data.str("");	
	while(true)
	{
		len = recv(fd, buff, sizeof(buff)-1, 0);
		if(len > 0)
		{
			buff[len] = '\0';
			ev->data << buff;
		}
		else
			break;
	}

	ev->remove(epollfd);
	if(errno == EAGAIN)
	{
		ev->reset(fd, send_data, (void*)0);
		ev->update(epollfd, EPOLLOUT|EPOLLET);
	}
	else
		close(fd);
}

void send_data(int fd, int events, void *arg)
{
	event_tag *ev = (event_tag*)arg;
	int len = read_data(fd, ev->data);

	ev->remove(epollfd);
	if(len > 0)
	{
		ev->reset(fd, recv_data, (void*)0);
		ev->update(epollfd, EPOLLIN|EPOLLET);
	}
	else
		close(fd);
}

int read_data(int clientfd, stringstream &data)
{
	int cgi = 0;
	string line, ret,
	       method, url, path, query;
	map<string, string> domain;
	struct stat st;

	getline(data, line);
	stringstream strm(line);
	strm >> method >> url;

	while(getline(data, line))
	{
		auto loc = line.find(": ");
		if(loc != string::npos)
		{
			string first, second;
			first.assign(line, 0, loc);
			second.assign(line, loc+2, string::npos);
			domain[first] = second;
		}
		else
			domain[""] = line;
	}

	if(method == "GET")
	{
		auto loc = url.find('?');
		if(loc != string::npos)
		{
			query.assign(url, loc+1, string::npos);
			url.resize(loc);
			cgi = 1;
		}

		auto iter = domain.find("Connection");
		if(iter != domain.cend())
			if(iter->second == "Upgrade")
				return upgrade_protocol(clientfd, domain);
	}
	else if(method == "POST")
		cgi = 1;
	else
		return -1;

	path = url;
	path.insert(0, "htdocs");
	if(*(--path.cend()) == '/')
		path += "index.html";
	if(stat(path.c_str(), &st) == -1)
		not_found(clientfd);
	else
	{
		if((st.st_mode & S_IFMT) == S_IFDIR)
			path += "index.html";
		if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode % S_IXOTH))
			cgi = 1;
		if(!cgi)
			return serve_file(clientfd, path);
		else
			return execute_cgi(clientfd, path, method, query, domain);
	}
	return 1;
}

int execute_cgi(int clientfd, const string path, const string method, const string query, const map<string, string> &domain)
{
	char buff[1024];
	int output[2];
	int input[2];
	pid_t pid;
	int status;
	int content_length = -1;
	char ch;
	string ret;

	if(method == "POST")
	{
		auto iter = domain.find("Content-Length");
		if(iter != domain.cend())
			content_length = atoi(iter->second.c_str());
		else
		{
			bad_request(clientfd);
			return -1;
		}
	}

	if((pipe(output) < 0) ||
			(pipe(input) < 0) ||
			((pid = fork()) < 0))
	{
		cannot_execute(clientfd);
		return -1;
	}

	if(pid == 0)
	{
		string method_env = method;
		string query_env = query;
		char length_env[255];
		char path_str[255];

		dup2(output[1], 1);
		dup2(input[0], 0);
		close(output[0]);
		close(input[1]);

		method_env.insert(0, "REQUEST_METHOD=");
		putenv((char*)method_env.c_str());

		if(method == "GET")
		{
			query_env.insert(0, "QUERY_STRING=");	
			putenv((char*)query_env.c_str());
		}
		else
		{
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			putenv(length_env);
		}

		strcpy(path_str, path.c_str());
		execl(path_str, path_str, NULL);
		exit(0);
	}
	else
	{
		close(output[1]);
		close(input[0]);

		if(method == "POST")
		{
			auto iter = domain.find("");
			if(iter != domain.cend())
				ret = iter->second;
			write(input[1], ret.c_str(), ret.length());
		}

		ret.clear();
		while(read(output[0], &ch, 1) > 0)
			ret += ch;

		close(output[0]);
		close(input[1]);
		waitpid(pid, &status, 0);
	}

	int len = ret.length();
	headers(clientfd, len);
	return len = send(clientfd, ret.c_str(), len, 0);
}

int upgrade_protocol(int clientfd, const map<string, string> &domain)
{
	return 1;
}

int serve_file(int clientfd, const string filename)
{
	ifstream fstrm(filename);
	string line, ret;

	while(getline(fstrm, line))
		ret += line;

	int len = ret.length();
	headers(clientfd, len);
	return len = send(clientfd, ret.c_str(), len, 0);
}

void headers(int clientfd, int len)
{
	char buff[1024];

	strcpy(buff, "HTTP/1.1 200 OK\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Server: jdbhttpd/0.1.0\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Content-Type: text/html\r\n");
	send(clientfd, buff, strlen(buff), 0);
	sprintf(buff, "Content-Length: %d\r\n", len);
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "\r\n");
	send(clientfd, buff, strlen(buff), 0);
}

void not_found(int clientfd)
{
	char buff[1024];
	char html[] = "<HTML><TITLE>Not Found</TITLE>\r\n\
		       <BODY><P>The servlet could not fulfill your request because the resource specified is unavailable or nonexisted.\r\n\
		       </BODY></HTML>\r\n";

	strcpy(buff, "HTTP/1.1 404 NOT FOUND\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Server: jdbhttpd/0.1.0\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Content-Type:text/html\r\n");
	send(clientfd, buff, strlen(buff), 0);
	sprintf(buff, "Content-Length: %d\r\n", sizeof(html));
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, html);
	send(clientfd, buff, strlen(buff), 0);
}

void bad_request(int clientfd)
{
	char buff[1024];
	char html[] = "<P>Your browser sent a bad request.\r\n";

	strcpy(buff, "HTTP/1.1 400 BAD REQUEST\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Server: jdbhttpd/0.1.0\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Content-Type:text/html\r\n");
	send(clientfd, buff, strlen(buff), 0);
	sprintf(buff, "Content-Length: %d\r\n", sizeof(html));
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, html);
	send(clientfd, buff, strlen(buff), 0);
}

void cannot_execute(int clientfd)
{
	char buff[1024];
	char html[] = "<P>Error prohibited CGI execution.\r\n";

	strcpy(buff, "HTTP/1.1 500 Internal Server Error\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Server: jdbhttpd/0.1.0\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "Content-Type:text/html\r\n");
	send(clientfd, buff, strlen(buff), 0);
	sprintf(buff, "Content-Length: %d\r\n", sizeof(html));
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, "\r\n");
	send(clientfd, buff, strlen(buff), 0);
	strcpy(buff, html);
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

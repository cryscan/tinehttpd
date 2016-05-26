#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include <regex>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
#include "event.h"

int http_proc(int, stringstream &, void *);
int websocket_proc(int, stringstream &, void *);
int serve_file(int, const string);
int execute_cgi(int, const string, const string, const string, const map < string, string > &);
int upgrade_protocol(int, const map < string, string > &, void *);
void headers(int, int);
void not_found(int);
void bad_request(int);
void cannot_execute(int);

extern string binary_path, data_path;
const regex pattern("(.*?):(\\s)?(.*)\\r", regex::optimize);

int http_proc(int clientfd, stringstream & data, void *arg)
{
	int cgi = 0;
	string line, ret, method, url, path, query;
	map < string, string > domain;
	smatch match;
	struct stat st;

	getline(data, line);
	stringstream strm(line);
	strm >> method >> url;

	while (getline(data, line))
		if (regex_match(line, match, pattern))
			domain[match.str(1)] = match.str(3);
		else
			domain[""] = line;

	if (method == "GET")
	{
		auto loc = url.find('?');
		if (loc != string::npos)
		{
			query.assign(url, loc + 1, string::npos);
			url.resize(loc);
			cgi = 1;
		}

		auto iter = domain.find("Connection");
		if (iter != domain.cend())
			if (iter->second == "Upgrade")
				return upgrade_protocol(clientfd, domain, arg);
	}
	else if (method == "POST")
		cgi = 1;
	else
		return -1;

	path = url;
	path.insert(0, data_path);
	if (*(--path.cend()) == '/')
		path += "index.html";
	if (stat(path.c_str(), &st) == -1)
		not_found(clientfd);
	else
	{
		if ((st.st_mode & S_IFMT) == S_IFDIR)
			path += "index.html";
		if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode % S_IXOTH))
			cgi = 1;
		if (!cgi)
			return serve_file(clientfd, path);
		else
			return execute_cgi(clientfd, path, method, query, domain);
	}
	return 1;
}

int serve_file(int clientfd, const string filename)
{
	ifstream fstrm(filename);
	string line, ret;

	while (getline(fstrm, line))
		ret += line;

	int len = ret.length();
	headers(clientfd, len);
	return len = send(clientfd, ret.c_str(), len, 0);
}

int execute_cgi(int clientfd, const string path, const string method,
				const string query, const map < string, string > &domain)
{
	char buff[1024];
	int output[2];
	int input[2];
	pid_t pid;
	int status;
	int content_length = -1;
	char ch;
	string ret;

	if (method == "POST")
	{
		auto iter = domain.find("Content-Length");
		if (iter != domain.cend())
			content_length = atoi(iter->second.c_str());
		else
		{
			bad_request(clientfd);
			return -1;
		}
	}

	if ((pipe(output) < 0) || (pipe(input) < 0) || ((pid = fork()) < 0))
	{
		cannot_execute(clientfd);
		return -1;
	}

	if (pid == 0)
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
		putenv((char *)method_env.c_str());

		if (method == "GET")
		{
			query_env.insert(0, "QUERY_STRING=");
			putenv((char *)query_env.c_str());
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

		if (method == "POST")
		{
			auto iter = domain.find("");
			if (iter != domain.cend())
				ret = iter->second;
			write(input[1], ret.c_str(), ret.length());
		}

		ret.clear();
		while (read(output[0], &ch, 1) > 0)
			ret += ch;

		close(output[0]);
		close(input[1]);
		waitpid(pid, &status, 0);
	}

	int len = ret.length();
	headers(clientfd, len);
	return len = send(clientfd, ret.c_str(), len, 0);
}

int upgrade_protocol(int clientfd, const map < string, string > &domain, void *arg)
{
	event_tag *ev = (event_tag *) arg;
	return 1;
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

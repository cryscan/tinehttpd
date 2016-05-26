#ifndef cytech_event_h
#define cytech_event_h

struct event_tag
{
	int fd;
	void (*call_back) (int, int, void *);
	int events;
	void *arg;
	int status;
	int (*proc) (int, stringstream &, void *);
	stringstream data;

	void reset(int, void (*)(int, int, void *), void *);
	void update(int, int);
	void remove(int);
};

#endif

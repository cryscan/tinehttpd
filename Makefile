all:httpd

httpd:httpd.cc
	g++ -W -Wall -std=c++11 -lc -o httpd httpd.cc

clean:
	-rm httpd

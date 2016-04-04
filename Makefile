all:httpd altnet

httpd:httpd.cc
	g++ -W -Wall -std=c++11 -lc -o httpd httpd.cc

altnet:altnet.c
	gcc -W -Wall -lc -o altnet altnet.c
	-cp altnet ./htdocs

clean:
	-rm httpd altnet

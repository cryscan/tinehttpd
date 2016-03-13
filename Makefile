all:httpd

httpd:httpd.c
	gcc -W -Wall -lc -o httpd httpd.c

clean:
	-rm httpd

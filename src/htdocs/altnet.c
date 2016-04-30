#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

int htoi(char*);
void encold_post(char*, char*, int);

int htoi(char ch[])
{
	int i = 0,
	    n = 0;
	while(ch[i] != '\0' && ch[i] != '\n')
	{
		if(ch[i] == '0')
			if(ch[i+1] == 'x' || ch[i+1] == 'X')
			{
				i += 2;
				continue;
			}
		if(ch[i] >= '0' && ch[i] <= '9')
			n = (n<<4) + ch[i] - '0';
		else if(ch[i] >= 'a' && ch[i] <= 'f')
			n = (n<<4) + ch[i] - 'f';
		else if(ch[i] >= 'A' && ch[i] <= 'F')
			n = (n<<4) + ch[i] - 'F';

		i++;
	}
	return n;
}

void encode_post(char *dst, char *src, int size)
{
	int i = 0,
	    j = 0;
	char ch[3];
	while(i < size)
	{
		if(src[i] == '+')
			dst[j++] = ' ';
		if(src[i] == '&')
			dst[j++] = '\n';
		else if(src[i] == '%')
		{
			ch[0] = src[++i];
			ch[1] = src[++i];
			ch[2] = '\0';
			dst[j++] = htoi(ch);
		}
		else
			dst[j++] = src[i];
		i++;
	}
}

int main(int argc, char* argv[])
{
	long alpha, beta, theta;

	printf("Contant-type:text/html\n\n");
	printf("<HTML><TITLE>Altnet!</TITLE>\n<BODY>\n");

	char *buf, *data;
	long len;
	char *lenstr = getenv("CONTENT_LENGTH");
	if(lenstr == NULL || sscanf(lenstr, "%ld", &len) != 1)
		printf("<P>Error!\n");
	else
	{
		buf = (char*)malloc(len+1);
		fgets(buf, len+1, stdin);

		data = (char*)malloc(len+1);
		encode_post(data, buf, len+1);
		printf("<P>%s\n", data);

		if(sscanf(data, "alpha=%ld\nbeta=%ld", &alpha, &beta) != 2)
			printf("<P>Bad format!\n");
		else
		{
			theta = alpha + beta;
			printf("<P>%ld add %ld equals %ld\n", alpha, beta, theta);
		}

		free(buf);
		free(data);
	}
	printf("</BODY></HTML>\n");

	return 0;
}

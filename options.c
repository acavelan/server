#include <stdio.h>
#include <regex.h>
#include <string.h>
#include <stdlib.h>

#include "options.h"

void usage(const char *progname)
{
	printf("usage: %s [-h] [-v] [-a address] [-p port]\n", progname);
}

int set_port(int *port, char *opt)
{
	int tmp = atoi(opt);

	if(tmp <= 0 || tmp >= 65536)
	{
		printf("Invalid port: %s\n", opt);
		return -1;
	}
	else
		*port = tmp;

	return 0;
}

int set_address(char *address, char *opt)
{	
	if(strcmp("localhost", opt) == 0)
	{
		strcpy(address, opt);
		return 0;
	}

	regex_t regex;
	int ret = regcomp(&regex, 
	            "^(([0-9]|[1-9][0-9]|1([0-9][0-9])|2([0-4][0-9]|5[0-5])).){3}"
	             "([0-9]|[1-9][0-9]|1([0-9][0-9])|2([0-4][0-9]|5[0-5]))$",
	             REG_EXTENDED);

	if(ret != 0)
	{
	    printf("Could not compile regex\n");
	    return -1;
	}

	ret = regexec(&regex, opt, 0, NULL, 0);

	if(ret == 0)
		strcpy(address, opt);
	else if(ret == REG_NOMATCH)
	{
	    printf("Invalid address: %s\n", opt);
	    return -1;
	}
	else
	{
		char msgbuf[128];
	    regerror(ret, &regex, msgbuf, sizeof(msgbuf));
	    printf("Regex match failed: %s\n", msgbuf);
	    return -1;
	}

	return 0;
}

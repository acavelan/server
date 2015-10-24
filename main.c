#include <stdio.h>
#include <signal.h>

#include "server.h"

void on_sig(int sig)
{
	printf("Signal caught: %d\n", sig);
}

int main(int argc, char **argv)
{
	struct server_t server;

	signal(SIGINT, on_sig);

	if(server_init(&server, argc, argv) == -1)
		return 0;

	if(server_start(&server) == -1)
		return 0;

	putlog(&server, INFO, "Listening on %s:%d ... (Verbose %d)\n", server.address, server.port, server.verbose);

	while(1)
	{
		int n = server_poll_events(&server);

		/* An error has occurred or a signal has been caught */
		if(n == -1)
			break;
	}

	server_destroy(&server);

	return 0;
}
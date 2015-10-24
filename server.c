#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <arpa/inet.h>

#include "options.h"
#include "server.h"

inline int check(int ret, const char *msg)
{
	if(ret == -1)
		perror(msg);
	return ret;
}

void putlog(struct server_t *server, int log_level, const char *format, ...)
{
	va_list args;
    
    if(server->verbose < log_level)
        return;

    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}

/* Options */
int parse_options(int argc, char **argv, struct server_t *server)
{
	struct option options[] =
	{
		{"help",    no_argument, 		0,  'h' },
		{"verbose", no_argument,       	0,  'v' },
		{"address", required_argument, 	0,  'a' },
		{"port",  	required_argument, 	0,  'p' },
		{0,         0,                 	0,  0 	}
	};

	int status = 0;
	
	while(1)
   	{
		int opt = getopt_long(argc, argv, "hva:p:", options, 0);

		if(opt == -1)
			break;

		switch(opt)
		{
			case 'h': status = -1; usage(argv[0]); break;
      		case 'v': server->verbose++; break;
       		case 'a': status = set_address(server->address, optarg); break;
       		case 'p': status = set_port(&server->port, optarg); break;
       		default: status++;
       }
   }

   if(optind < argc)
   {
       while(optind < argc)
           printf("Invalid argument -- '%s'\n", argv[optind++]);
   }

   return status;
}

/* Epoll */
int make_socket_non_blocking(int sfd)
{
	int flags = check(fcntl(sfd, F_GETFL, 0), "fcntl");
	if(flags == -1)
		return -1;

	int ret = check(fcntl(sfd, F_SETFL, flags | O_NONBLOCK), "fcntl");
	if(ret == -1)
		return -1;

	return 0;
}

int add_ptr_to_epoll(int efd, int sfd, void *ptr)
{
	struct epoll_event event;

	memset(&event, 0, sizeof(event));

	event.data.ptr = ptr;
	event.events = EPOLLIN | EPOLLET;

	return check(epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event), "epoll_ctl");
}

int create_and_bind(const char *address, int port)
{
	struct sockaddr_in saddr;

	memset(&saddr, 0, sizeof(saddr));

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = inet_addr(address);

	int sfd = check(socket(AF_INET, SOCK_STREAM, 0), "socket");

	if(sfd == -1)
		return -1;

	int ret = check(bind(sfd, (struct sockaddr*)&saddr, sizeof(saddr)), "bind");
	if(ret == -1)
	{
		close(sfd);
		return -1;
	}

	ret = check(listen(sfd, SOMAXCONN), "listen");
	if(ret == -1)
	{
		close(sfd);
		return -1;
	}

	return sfd;
}

/* Nodes */
struct node_t* server_accept_connection(struct server_t *server)
{
	if(server->count == MAXNODES)
	{
		putlog(server, ERROR, "ERROR: Maximum number of nodes has been reached.\n");
		return 0;
	}

	struct sockaddr in_addr;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	socklen_t in_len = sizeof(in_addr);

	int infd = accept(server->sfd, &in_addr, &in_len);
	if(infd == -1)
	{
		if((errno != EAGAIN) && (errno != EWOULDBLOCK))
			perror("accept");
		/* else: We have processed all incoming connections. */
		return 0;
	}

	if(getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
		putlog(server, INFO, "INFO: New connection on descriptor %d (host=%s, port=%s)\n", infd, hbuf, sbuf);

	return server_add_node(server, infd);
}

struct node_t* server_add_node(struct server_t *server, int fd)
{
	struct node_t *node = (struct node_t*)calloc(1, sizeof(node_t));

	if(node == 0)
	{
		putlog(server, ERROR, "ERROR: Unable to allocate node memory.\n");
		return 0;
	}

	node->id = server->count;
	node->fd = fd;

	server->nodes[server->count++] = node;

	/* Make the incoming socket non-blocking */
	if(make_socket_non_blocking(fd) == -1)
	{
		putlog(server, ERROR, "ERROR: Unable to make socket non-blocking.\n");
		server_remove_node(server, node);
		return 0;
	}

	/* Then add it to the list of fds to monitor. */
	if(add_ptr_to_epoll(server->efd, fd, (void*)node) == -1)
	{
		putlog(server, ERROR, "ERROR: Unable to add node.\n");
		server_remove_node(server, node);
		return 0;
	}

	return node;
}

void server_remove_node(struct server_t *server, struct node_t *node)
{
	server->nodes[node->id] = server->nodes[--server->count];
	server->nodes[node->id]->id = node->id;

	/* Note: closing a descriptor will make epoll remove it
	from the set of descriptors which are monitored. */
	close(node->fd);
	free(node);
}

int node_read_buffer(struct node_t *node)
{
	int alive = 1;
	ssize_t count = 0;
	do
	{
		char buf[512];

		count = read(node->fd, buf, sizeof(buf));
		if(count == -1)
		{
			/* if errno == EAGAIN, that means we have read all data */
			if(errno != EAGAIN)
			{
				perror("read");
				alive = -1;
			}
		}
		/* End of file. The remote has closed the connection. */
		else if(count == 0)
			alive = -1;
		/* Write the buffer to standard output */
		else
			check(write(1, buf, count), "write");
	}
	while(count > 0 && alive);

	return alive;
}

int server_init(struct server_t *server, int argc, char **argv)
{
	server->verbose = 0;
	server->port = 4242;
	server->master = 0;
	server->count = 0;
	
	strcpy(server->address, "0.0.0.0");

	return parse_options(argc, argv, server);
}

int server_start(struct server_t *server)
{
	server->sfd = create_and_bind(server->address, server->port);
	if(server->sfd == -1)
		return -1;

	server->efd = check(epoll_create1(0), "epoll_create1");
	if(server->efd == -1)
		return -1;

	server->master = server_add_node(server, server->sfd);
	if(server->master == 0)
		return -1;

	server->events = (struct epoll_event*)calloc(MAXEVENTS, sizeof(struct epoll_event));
	if(server->events == 0)
	{
		putlog(server, ERROR, "ERROR: Unable to allocate events memory.\n");
		return -1;
	}

	return 0;
}

void server_destroy(struct server_t *server)
{
	while(server->count > 0)
		server_remove_node(server, server->nodes[server->count-1]);
	free(server->events);
}

int server_poll_events(struct server_t *server)
{
	int i, n = check(epoll_wait(server->efd, server->events, MAXEVENTS, -1), "epoll_wait");

	for(i = 0; i < n; i++)
	{
		struct epoll_event *event = &server->events[i];
		struct node_t *node = (struct node_t*)event->data.ptr;

		if(	(event->events & EPOLLERR) || /* error on fd */
			(event->events & EPOLLHUP) || /* or hang up */
			(!(event->events & EPOLLIN))) /* or not ready */
		{
			/* An error has occured on this fd, or the socket is not
			ready for reading (why were we notified then?) */
			putlog(server, INFO, "INFO: Closed connection on descriptor %d (inconsistent state).\n", node->fd);
			server_remove_node(server, node);
		}
		else if(server->master->fd == node->fd)
		{
			/* There is a notification on the listening fd, which
			means one or more incoming connections. */
			while(server_accept_connection(server) != 0);
		}
		else
		{
			/* We have data on the fd waiting to be read. Read and
			display it. We must read whatever data is available
			completely, as we are running in edge-triggered mode
			and won't get a notification again for the same data. */
			int alive = node_read_buffer(node);

			if(alive == -1)
			{
				putlog(server, INFO, "INFO: Closed connection on descriptor %d\n", node->fd);
				server_remove_node(server, node);
			}
		}
	}

	return n;
}

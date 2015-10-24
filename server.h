#ifndef SERVER_H
#define SERVER_H

#define MAXEVENTS 64
#define MAXNODES 512

struct epoll_event;

enum log_level_t { ERROR=0, INFO, DEBUG };

struct node_t
{
	int id;
	int fd;
};
typedef struct node_t node_t;

struct server_t
{
	int verbose;

	int port;
	char address[16];

	int sfd;
	int efd;

	int count;
	struct node_t *master;
	struct node_t *nodes[MAXNODES];

	struct epoll_event *events;
};
typedef struct server_t server_t;

/* Debug */
int check(int ret, const char *msg);

void putlog(struct server_t *server, int log_level, const char *format, ...);

/* Options */
int parse_options(int argc, char **argv, struct server_t *server);

/* Epoll */
int make_socket_non_blocking(int sfd);

int add_ptr_to_epoll(int efd, int sfd, void *ptr);

int create_and_bind(const char *address, int port);

/* Nodes */
struct node_t* server_accept_connection(struct server_t *server);

struct node_t* server_add_node(struct server_t *server, int fd);

void server_remove_node(struct server_t *server, struct node_t *node);

int node_read_buffer(struct node_t *node);

/* Server */
int server_init(struct server_t *server, int argc, char **argv);

int server_start(struct server_t *server);

void server_destroy(struct server_t *server);

int server_poll_events(struct server_t *server);

#endif
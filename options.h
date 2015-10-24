#ifndef OPTIONS_H
#define OPTIONS_H

void usage(const char *progname);

int set_port(int *port, char *opt);

int set_address(char *address, char *opt);

#endif
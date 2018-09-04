#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#include "net.h"
#include "integrate.h"

#define LISTEN_PORT 25435
#define BROADCAST_PORT 25440
#define TIMEOUT 1000
#define RECALL_ATTEMPTS 10

const char * hello = "Hello, my dear friend.\n";
const struct timeval SOCKET_IO_TIMEOUT = {.tv_sec = 1, .tv_usec = 0};

void sendBroadcast(int nServers)
{
	struct sockaddr_in addr;
	int true = 1;

	int broadcast_socket = socket(PF_INET, SOCK_DGRAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST, &true, sizeof(true));

	char checkbuf[strlen(hello) + 1];
	strcpy(checkbuf, hello);
	
	for (int i = 0; i < nServers; i++)
	{
		addr.sin_port = htons(BROADCAST_PORT + i);
		sendto(broadcast_socket, checkbuf, sizeof(checkbuf), 0, (struct sockaddr*) &addr, sizeof(addr));
	}

	close(broadcast_socket);
}

int configureListenSocket(int nServers)
{
	struct sockaddr_in addr;
	int true = 1;
	int listen_socket = socket(PF_INET, SOCK_STREAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(LISTEN_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(true));

	int one = 1;

	setsockopt(listen_socket, SOL_SOCKET, SO_KEEPALIVE, &true, sizeof(true));
	setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPCNT, &one, sizeof(one));
	setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPIDLE, &one, sizeof(one));
	setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPINTVL, &one, sizeof(one));

	
	if (errno != 0)
	{
		fprintf(stderr, "Failed to set listen socket options: %s (%d)\n", strerror(errno), errno);
		close(listen_socket);
		return -1;
	}

	bind(listen_socket, (struct sockaddr*) &addr, sizeof(addr));
	if (errno != 0)
	{
		fprintf(stderr, "Failed to bind listen socket: %s (%d)\n", strerror(errno), errno);
		close(listen_socket);
		return -1;
	}
	listen(listen_socket, nServers);

	return listen_socket;
}

int openConnections(struct Connection** conp, int nServers)
{
	struct sockaddr_in addr;

	*conp = malloc(sizeof(struct Connection) * nServers);
	if (errno != 0)
	{
		fprintf(stderr, "Failed to allocate memory for connections array.\n");
		return -1;
	}

	struct Connection* con = *conp;

	int listen_socket = configureListenSocket(nServers);
	if (listen_socket == -1)
		return -1;

	sendBroadcast(nServers);
	
	struct pollfd pollfd;
	pollfd.events = POLLIN;
	pollfd.fd = listen_socket;

	int attempts = RECALL_ATTEMPTS;

	socklen_t addr_len = sizeof(addr);
	for (int i = 0; i < nServers; i++)
	{
		pollConnections:
		poll(&pollfd, 1, TIMEOUT);

		if ((pollfd.revents & POLLIN) == 0)
		{
			fprintf(stderr, "Looks like someone is sleeping... Sending broadcast again. %d left.\n", attempts);
			sendBroadcast(nServers);
			if (attempts-- == 0)
			{
				fprintf(stderr, "Couldn't find all the %d servers. Stopping...\n", nServers);
				return -1;
			}
			goto pollConnections;
		}
		attempts = RECALL_ATTEMPTS;

		int fd = accept4(listen_socket, (struct sockaddr*) &addr, &addr_len, SOCK_NONBLOCK);
		if (errno != 0)
		{
			fprintf(stderr, "An error occured while connecting to %dth server: %s (%d)\n", i, strerror(errno), errno);
			return -1;
		}

		con[i] = makeConnection(fd, fd);
		fprintf(stderr, "Connected to server %d at address %s:%hu\n", i, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	}

	close(listen_socket);	
	return 0;
}

void closeConnections(struct Connection* con, int nServers)
{
	for (int i = 0; i < nServers; i++)
	{
		close(con[i].rd);
		close(con[i].wr);
	}

	free(con);
}

int serverWaitForJob(int threadNumber)
{
	int true = 1;
	struct sockaddr_in addr;

	int fd = socket(PF_INET, SOCK_DGRAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(BROADCAST_PORT + threadNumber);

	socklen_t addr_len = sizeof(addr);

	bind(fd, (struct sockaddr*) &addr, sizeof(addr));
	if (errno != 0)
	{
		fprintf(stderr, "Failed to bind broadcast listening socket to port %hu\n", ntohs(addr.sin_port));
		fprintf(stderr, "This is probably because of another server running on this machine.\n");
		kill(0, SIGTERM);
	}

	waitForInvitation:
	fprintf(stderr, "Waiting for invitation from client...\n");
	char checkbuf[strlen(hello) + 1];
	recvfrom(fd, checkbuf, sizeof(checkbuf), 0, (struct sockaddr*) &addr, &addr_len);
	if (strcmp(checkbuf, hello) != 0)
	{
		if (errno != 0)
			goto errReturn;
		fprintf(stderr, 
"\033[91m\033[1m\
Someone attempted to connect to our mining server!\033[0m \
IP: %s:%hu\n", 
				inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		goto waitForInvitation;
	}
	addr.sin_port = htons(LISTEN_PORT);

	close(fd);

	fd = socket(PF_INET, SOCK_STREAM, 0);

	int one = 1;

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &true, sizeof(true));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &one, sizeof(one));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &one, sizeof(one));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &one, sizeof(one));

	if (errno != 0)
	{
		fprintf(stderr, "Failed to set socket options: %s (%d)\n", strerror(errno), errno);
		close(fd);
		return -1;
	}


	fprintf(stderr, "Connecting to client at address %s:%hu\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	connect(fd, (struct sockaddr*) &addr, addr_len);
	if (errno != 0)
		goto errReturn;

	return fd;

	errReturn:
	fprintf(stderr, "Failed to connect to client: %s (%d)\n", strerror(errno), errno);
	close(fd);
	return -1;
}

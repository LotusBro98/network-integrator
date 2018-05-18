#define _POSIX_C_SOURCE 2
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>



#include "ui.h"
#include "general.h"
#include "net.h"
#include "integrate.h"
#include "cpuconf.h"


int client(double left, double right, double maxDeviation, int nServers)
{
	enum ErrorCode error = ERR_NO_ERROR;

	struct Connection* con;

	if (openConnections(&con, nServers) != 0)
		return EXIT_FAILURE;

	double I = parentIntegrate(con, nServers, left, right, maxDeviation, &error);

	if (error == ERR_NO_ERROR)
		printAnswer(left, right, maxDeviation, I);
	else
		explainError(error);

	closeConnections(con, nServers);

	return error == ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}

int serverThread(int threadNumber)
{
	attachChildToCPU(threadNumber);
	destroyCPUData();

	while(1)
	{
		int fd = serverWaitForJob(threadNumber);

		if (fd == -1)
		{
			errno = 0;
			continue;
		}

		fprintf(stderr, "Calculating stuff...\n");

		childCalcSums(fd, fd);

		if (errno != 0)
		{
			fprintf(stderr, "An error has occured during calculation: %s (%d)\n", strerror(errno), errno);
			errno = 0;
		}

		close(fd);

/*
		if (errno != 0)
		{
			fprintf(stderr, "Thread %d: error %d: %s", threadNumber, errno, strerror(errno));
			return EXIT_FAILURE;
		}
*/

		fprintf(stderr, "Finished calculations. Waiting for further jobs.\n");
	}

	return EXIT_SUCCESS;
}

int server(int nThreads)
{
	initCPUData();

	for (int i = 0; i < nThreads; i++)
	{
		if (fork() == 0)
			return serverThread(i);

		if (errno != 0)
			kill(0, SIGTERM);
	}

	for (int i = 0; i < nThreads; i++)
		wait(NULL);

	return 0;
}

int main(int argc, char* argv[])
{
	double left = 0;
	double right = 1;
	int nChildren;
	double maxDeviation = 1e-15;

	if (parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation))
		return server(nChildren);
	else
		return client(left, right, maxDeviation, nChildren);

	return 0;
}

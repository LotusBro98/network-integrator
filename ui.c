#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include "ui.h"

struct timeval start;
struct timeval lastPrint;
int firstTime = TRUE;

long getMillis(struct timeval tv)
{
	return (tv.tv_sec % 10000) * 1000 + tv.tv_usec / 1000;
}

long getMicros(struct timeval tv)
{
	return (tv.tv_sec % 100) * 1000000 + tv.tv_usec;
}


void initTiming(struct ChildAnswer* answers, int nChildren)
{
	gettimeofday(&start, NULL);

	for (int i = 0; i < nChildren; i++)
	{
		answers[i].sent = start;
		answers[i].received = start;
		answers[i].sentBack = start;
	}
}

void exitError()
{
	fprintf(stderr, "Error %d: %s\n", errno, strerror(errno));
	exit(EXIT_FAILURE);
}


void exitErrorMsg(char* description)
{
	fprintf(stderr, "%s", description);
	exit(EXIT_FAILURE);
}

void printAnswer(double left, double right, double maxDeviation, double I)
{
	fprintf(stderr, "\033M");
	fflush(stderr);
	for (int i = 0; i < 128; i++)
		fprintf(stderr, " ");
	
	fprintf(stderr,
		"\n"\
		"%7.3f\n"\
		"    /\n"\
		"    | f(x) dx = ",
		right
	);
	fflush(stderr);

	char fmt[8];
	sprintf(fmt, "%%.%dlf", (int)(-log10(maxDeviation) - 0.001) + 1);

	printf(fmt, I);
	fflush(stdout);

	fprintf(stderr, " +/- %lg\n"\
		"    /\n"\
		"%7.3f\n"\
		"\n",
		maxDeviation, left
	);
}

void printProgress(struct SegmentList segList, double left, double right, double I)
//void printProgress(struct SegmentList segList, double left, double right, double I, struct ChildAnswer* answers, int nChildren)
{
	int dots = 100;
	double unitsPerDot = (right - left) / dots;
	double x = left;
	struct UnstudiedSegment* p = segList.head->next;

	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (p != segList.head && getMillis(tv) - getMillis(lastPrint) < PRINT_PERIOD_MS)
		return;
	else
		lastPrint = tv;

	if (firstTime)
		firstTime = FALSE;
	else
	{
		fprintf(stderr, "\033M");
//		for (int i = 0; i < nChildren + 3; i++)
//			fprintf(stderr, "\033M");
		fflush(stdout);
	}

	
	while (x < right)
	{
		if (p != segList.head && x > p->left)
			fprintf(stderr, "\033[91m-\033[0m");
		else
			fprintf(stderr, "\033[93m+\033[0m");

		while (p != segList.head && p->next != segList.head && x > p->next->left)
			p = p->next;

		x += unitsPerDot;
	}

	fprintf(stderr, "  %.18lf\n", I);

//	printTimes(answers, nChildren);
}

void printTimes(struct ChildAnswer* answers, int nChildren)
{
	long minTime = getMicros(answers[0].sent);
	long maxTime = getMicros(answers[0].sentBack);

	for (int i = 0; i < nChildren; i++)
	{
		if (getMicros(answers[i].sent) < minTime)
			minTime = getMicros(answers[i].sent);

		if (getMicros(answers[i].sentBack) > maxTime)
			maxTime = getMicros(answers[i].sentBack);
	}

	int dots = 100;
	double microsPerDot = (maxTime - minTime) / (double)dots;
	double startTime = minTime;

	for (int i = 0; i < dots + 10; i++)
		printf(" ");
	printf("\n");
	
	printf("%4ld", minTime - getMicros(start));
	for (int i = 8; i < dots; i++)
		printf("-");
	printf("%4ld\n", maxTime - getMicros(start));

	for (int i = 0; i < dots + 2; i++)
		printf(" ");
	printf("\n");

	for (int n = 0; n < nChildren; n++)
	{
		if (microsPerDot != 0)
		{
			int i = 0;
			for (; startTime + i * microsPerDot < getMicros(answers[n].sent) && i < dots; i++)
				printf(" ");

			printf("\033[91m");
			for (; startTime + i * microsPerDot < getMicros(answers[n].received) && i < dots; i++)
				printf("*");
	
			printf("\033[92m");
			for (; startTime + i * microsPerDot < getMicros(answers[n].sentBack) && i < dots; i++)
				printf("*");
	
			for (i--; startTime + i * microsPerDot < maxTime && i < dots; i++)
				printf(" ");
		}

		printf("\033[0m\n");
	}
}

int parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation)
{
	if (argc < 3) goto printUsage;

	if (strcmp(argv[1], "server") == 0)	//Server
	{
		*nChildren = atoi(argv[2]);
		if (errno != 0) goto printUsage;	
		return 1;
	}
	else if (strcmp(argv[1], "client") == 0) //Client
	{
		*nChildren = atoi(argv[2]);
		if (errno != 0) goto printUsage;	

		if (argc == 4)	goto printUsage;

		if (argc < 5) return 0;

		*left = atof(argv[3]);
		*right = atof(argv[4]);
		if (errno != 0) goto printUsage;

		if (argc < 6) return 0;

		*maxDeviation = atof(argv[5]);
		if (errno != 0) goto printUsage;

		if (argc > 6) goto printUsage;

		return 0;
	}

	printUsage:
	exitErrorMsg(
"\n\
Client:		./net-integrate client <nServers> [<from> <to>] [maxDeviation]\n\
Server:		./net-integrate	server <nThreads>\n\n\
"
	);
	return 0;
}

void explainError(enum ErrorCode error)
{
	switch (error)
	{
		case ERR_NO_ERROR:
			fprintf(stderr, "Success.\n");
			break;
		case ERR_BEST_FINENESS_REACHED:
			fprintf(stderr, 
"\n\nBest possible fineness for type double has been reached. \
Further calculations will lead to accuracy loss or, possibly, to a deadlock. \
Please, try running the program again with bigger <maxDeviation>.\n\n");
			break;
		case ERR_OTHER:
			fprintf(stderr, "An error occured while integrating the function: %s (%d).\n Try relaunching the program.\n", strerror(errno), errno);
			break;
		case ERR_CHILD_DISCONNECTED:
			fprintf(stderr, "Lost connection with one of calculating processes. Relaunching the program may help.\n");
			break;
		case ERR_TIMEOUT:
			fprintf(stderr, "No response from any child within timeout. Exiting.\n");
			break;
	}
}

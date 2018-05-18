#ifndef GENERAL_H
#define GENERAL_H

#include <sys/time.h>

enum ErrorCode {
	ERR_NO_ERROR,
	ERR_BEST_FINENESS_REACHED,
	ERR_CHILD_DISCONNECTED,
	ERR_OTHER
};

struct Connection
{
	int rd;
	int wr;
	int waiting;
	int closed;
};

struct CalcRequest
{
	double left;
	double right;
	double dens;

	struct timeval sent;
};

struct ChildAnswer
{
	double S;
	double eps;

	struct timeval sent;
	struct timeval received;
	struct timeval sentBack;

	enum ErrorCode error;	
};

#define TRUE 1
#define FALSE 0

/*
#define MAX_SEGMENTS_PER_PROCESS 0x10000000
#define START_UNSTUDIED_SEGMENTS 0x100
#define START_LITTLE_SEGMENTS 0x10
*/

#endif

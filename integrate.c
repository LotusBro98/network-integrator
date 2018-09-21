#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/select.h>
#include <wait.h>
#include <sched.h>
#include <math.h>
#include <fcntl.h>

#include "ui.h"
#include "list.h"
#include "general.h"
#include "cpuconf.h"
#include "integrate.h"

enum requestOrder { RQ_FIRST, RQ_LAST };

struct Connection makeConnection(int rd, int wr)
{
	return (struct Connection) {.rd = rd, .wr = wr, .closed = FALSE, .waiting = TRUE};
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq, int child, enum requestOrder order, double dens)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->dens = dens;

	gettimeofday(&(rq->sent), NULL);

	if (order == RQ_FIRST)
		seg->child = child + 1;
	else
		seg->child = -(child + 1);
}

void closeChild(struct Connection* con, struct SegmentList segList, int child)
{
	struct UnstudiedSegment* seg;

	seg = getSeg(segList, child + 1);
	if (seg != NULL)
		seg->child = 0;

	seg = getSeg(segList, -(child + 1));
	if (seg != NULL)
		seg->child = 0;

	con[child].closed = TRUE;
	//fprintf(stderr, "Lost connection with child %d: %s (%d)\n", child, strerror(errno), errno);
	fprintf(stderr, "Lost connection with child %d\n", child);
}

void sendRequest
(struct Connection* con, struct SegmentList segList, struct UnstudiedSegment* seg, 
int child, enum requestOrder order, double dens, enum ErrorCode* error)
{
	struct CalcRequest rq;
	int bytesWritten;

	makeRequest(seg, &rq, child, order, dens);
	bytesWritten = write(con[child].wr, &rq, sizeof(rq));
	if (errno != 0 || bytesWritten != sizeof(rq))
	{
		closeChild(con, segList, child);
		errno = 0;
		*error = ERR_CHILD_DISCONNECTED;
	}

	con[child].waiting = FALSE;
}

void handleSegmentData
(struct Connection* con, struct SegmentList segList, struct ChildAnswer* ans, int child, double* I, double dens, enum ErrorCode* error)
{
	struct UnstudiedSegment* seg = getSeg(segList, child + 1);

	if (ans->eps < dens)
	{
		seg->S = ans->S;
		*I += removeSeg(seg);
	}
	else
	{
		split(seg);
	}

	seg = getSeg(segList, -(child + 1));
	if (seg != NULL)
		seg->child = child + 1;
	else
	{
		seg = getSeg(segList, 0);
		if (seg == NULL)
		{
			con[child].waiting = TRUE;
			return;
		}
		
		sendRequest(con, segList, seg, child, RQ_FIRST, dens, error);
	}

	seg = getSeg(segList, 0);
	if (seg != NULL)
		sendRequest(con, segList, seg, child, RQ_LAST, dens, error);
}

int isClosed(struct Connection* con, int nChildren)
{
	for (int i = 0; i < nChildren; i++)
		if (!con[i].closed)
			return FALSE;
	return TRUE;
}

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation, enum ErrorCode* error)
{
	double dens = maxDeviation / (right - left);
	struct ChildAnswer ans;
	struct SegmentList segList = initList(left, right);
	struct UnstudiedSegment* seg;
	double I = 0;
	fd_set rd;

	while (!isEmpty(segList))
	{
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed) && con[i].waiting && (seg = getSeg(segList, 0)) != NULL)
				sendRequest(con, segList, seg, i, RQ_FIRST, dens, error);
		
		if (isClosed(con, nChildren))
			*error = ERR_OTHER;

		if (*error != ERR_NO_ERROR)	break;

		FD_ZERO(&rd);
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed))
				FD_SET(con[i].rd, &rd);

		struct timeval SOCKET_IO_TIMEOUT = {SOCKET_IO_TIMEOUT_SEC, 0};
		if (select(nChildren * 2 + 10, &rd, NULL, NULL, &SOCKET_IO_TIMEOUT) <= 0)
		{
			if (errno == 0)
				*error = ERR_TIMEOUT;
			else
				*error = ERR_OTHER;
			break;
		}

		for (int i = 0; i < nChildren; i++)
			if (FD_ISSET(con[i].rd, &rd))
			{
				if (read(con[i].rd, &ans, sizeof(ans)) == 0 || errno != 0 || ans.error != ERR_NO_ERROR)
				{
					closeChild(con, segList, i);
					if (ans.error == ERR_NO_ERROR)
						*error = ERR_CHILD_DISCONNECTED;
					else
						*error = ans.error;
					break;
				}

				handleSegmentData(con, segList, &ans, i, &I, dens, error);
				if (*error != ERR_NO_ERROR) break;
			}

		if (*error != ERR_NO_ERROR) break;
		printProgress(segList, left, right, I);
	}

	destroyList(segList);

	return I;
}

void calcSums(double left, double right, double* I, double* eps, enum ErrorCode* error)
{
	const int nSegments = 0x1000;
	const int nSubSegments = 0x100;
	
	if ((right - left) / nSegments / nSubSegments < BEST_FINENESS)
	{
		*error = ERR_BEST_FINENESS_REACHED;
		return;
	}

	double DI = 0;
	double epsCur = 0;

	for (register int n = 0; n < nSegments; n++)
	{
		double l = left +  n      * (right - left) / nSegments;
		double r = left + (n + 1) * (right - left) / nSegments;

		register double dI = 0;
		register double dEps = 0;
		double fleft = f(l), fright = f(r);
		register double f1, f2 = 0; 
		register double x;
	
		double dt = 1.0 / nSubSegments;
		for (register double t = dt; t <= 1; t += dt)
		{
			f1 = f2;

			//x = l + t * (r - l);
			x = r;
			x -= l;
			x *= t;
			x += l;

			//f2 = f(x) - fleft - t * (fright - fleft);
			f2 = fleft;
			f2 -= fright;
			f2 *= t;
			f2 -= fleft;
			f2 += f(x);

			//dI += (f1 + f2);
			dI += f1;
			dI += f2;

			dEps += f1 > f2 ? f1 - f2 : f2 - f1;
		}

		dI /= 2;

		DI += dI / nSubSegments + (fright + fleft) / 2;
		epsCur += dEps / nSubSegments;
	}

	*eps = epsCur / (nSegments);
	*I = DI / nSegments * (right - left);
	*error = ERR_NO_ERROR;
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;
	int bytesWritten;
	int bytesRead;

	signal(SIGPIPE, SIG_IGN);

	while ((bytesRead = read(rd, &rq, sizeof(rq))))
	{
		if (bytesRead != sizeof(rq))
			break;

		gettimeofday(&ans.received, NULL);
		calcSums(rq.left, rq.right, &(ans.S), &(ans.eps), &(ans.error));
		gettimeofday(&ans.sentBack, NULL);

		bytesWritten = write(wr, &ans, sizeof(ans));
		gettimeofday(&ans.sent, NULL);
		if (errno != 0 || bytesWritten != sizeof(ans))
			break;
	}
}

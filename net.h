#ifndef NET_H
#define NET_H

#include "general.h"

int openConnections(struct Connection** con, int nServers);
void closeConnections(struct Connection* con, int nServers);

int serverWaitForJob(int threadNumber);

#endif

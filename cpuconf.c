#define _POSIX_C_SOURCE 2
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>

#include "cpuconf.h"

int maxCPU;
int maxCore;
int* coreForCPU;
int* CPUOrder;
int nCPUs;


void attachChildToCPU(int child)
{
	int cpu = getCPUForChild(child);

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);
	if (errno != 0)
	{
		fprintf(stderr, "Failed to attach child %d to CPU %d: %s (%d)\n", child, cpu, strerror(errno), errno);
		errno = 0;
	}
}

void parseCoresForCPU()
{
	FILE* info = popen("lscpu -p | tail -n +5", "r");

	int cpu, core;
	char bolt[256];

	maxCPU = -1;
	maxCore = -1;
	coreForCPU = NULL;

	while(fscanf(info, "%d,%d%s", &cpu, &core, bolt) > 0) 
	{
		if (cpu > maxCPU)
		{
			coreForCPU = realloc(coreForCPU, sizeof(int) * (cpu + 1));
			for (int i = maxCPU + 1; i <= cpu; i++)
				coreForCPU[i] = -1;

			maxCPU = cpu;
		}
		
		if (core > maxCore)
			maxCore = core;

		coreForCPU[cpu] = core;
	}

	pclose(info);

	nCPUs = 0;
	for (int i = 0; i <= maxCPU; i++)
		if (coreForCPU[i] != -1)
			nCPUs++;

	CPUOrder = malloc(nCPUs * sizeof(int));
}

int findCPUForNextCore(int core)
{
	int cpu = -1;
	for (int i = 0; i <= maxCPU; i++)
	{
		if (coreForCPU[i] > core && (cpu == -1 || coreForCPU[i] < coreForCPU[cpu]))
			cpu = i;
	}

	if (cpu == -1)
		return findCPUForNextCore(-1);

	return cpu;
}

void buildCPUOrder()
{
	int cpu;
	int core = -1;
	for (int i = 0; i < nCPUs; i++)
	{
		cpu = findCPUForNextCore(core);
		core = coreForCPU[cpu];
		coreForCPU[cpu] = -1;
		CPUOrder[i] = cpu;
	}

	free(coreForCPU);
}

void initCPUData()
{
	parseCoresForCPU();
	for (int i = 0; i <= maxCPU; i++)
		fprintf(stderr, "%d ", coreForCPU[i]);
	fprintf(stderr, "\n");

	buildCPUOrder();
	for (int i = 0; i < nCPUs; i++)
		fprintf(stderr, "%d ", CPUOrder[i]);
	fprintf(stderr, "\n");
}

void destroyCPUData()
{
	free(CPUOrder);
}

int getCPUForChild(int child)
{
	return CPUOrder[child % nCPUs];
}

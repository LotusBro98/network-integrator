#ifndef CPUCONF_H
#define CPUCONF_H

void initCPUData();
void destroyCPUData();
int getCPUForChild(int child);

void attachChildToCPU(int child);

#endif

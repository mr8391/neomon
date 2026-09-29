#pragma once

#include "sys.h"

void copyString(char* dst, size_t size, const char* src);
int copyValue(const char* line, size_t len, char* out, size_t size);
void addDisk(Info* info, const char* mount, unsigned long long used, unsigned long long total);
void sortDisks(Info* info);

void platIdentity(Info* info);
void platEnvironment(Info* info);
void platUptime(Info* info);
void platPackages(Info* info);
void platCpu(Info* info, int delayMs);
void platGpu(Info* info);
void platMem(Info* info);
void platSwap(Info* info);
void platDisk(Info* info);
void platNet(Info* info, int delayMs);
void platLoad(Info* info);
void platBattery(Info* info);

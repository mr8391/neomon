#pragma once

#include "../core/core.h"

#define MAX_DISKS 6

typedef struct {
	char mount[48];
	char pair[48];
	int pct;
} Disk;

typedef struct {
	char os[128];
	char kernel[128];
	char user[64];
	char host[128];
	char shell[64];
	char wm[64];
	char terminal[64];
	char uptime[32];
	char packages[96];

	char cpuModel[128];
	char cpuSpec[64];
	char cpuTemp[16];
	int cpuPct;

	char gpuModel[128];
	char gpuTemp[16];
	int gpuPct;
	int hasGpu;

	char gpuMemText[48];
	int gpuMemPct;
	int hasVram;

	char memText[48];
	int memPct;

	char swapText[48];
	int swapPct;
	int hasSwap;

	Disk disks[MAX_DISKS];
	int diskCount;

	char netName[32];
	char netIp[64];
	char netDown[32];
	char netUp[32];
	int hasNet;

	char load[48];
	char procs[48];

	char battery[64];
	int hasBattery;
} Info;

void getInfo(Info* info, int delayMs);

int parseOsRelease(const char* text, const char* key, char* out, size_t size);
int parseMemInfo(const char* text, const char* key, unsigned long long* outKb);
int parseCpuInfo(const char* text, char* model, size_t modelSize, int* threads, int* cores);
int parseCpuFreq(const char* text, double* outGhz);
int parseUptime(const char* text, double* outSeconds);
int parseLoadAvg(const char* text, char* load, size_t loadSize, char* procs, size_t procsSize);
int parseStatCpu(const char* text, unsigned long long* busy, unsigned long long* total);
int parseNetDev(const char* text, const char* iface, unsigned long long* rx, unsigned long long* tx);
int parseMilliC(const char* text, int* outCelsius);

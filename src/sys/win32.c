#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <tlhelp32.h>

static int registryString(const char* path, const char* key, char* out, size_t size) {
	HKEY handle;
	DWORD type = 0;
	DWORD length = (DWORD)size;

	out[0] = '\0';
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &handle) != ERROR_SUCCESS)
		return 0;
	if (RegQueryValueExA(handle, key, NULL, &type, (LPBYTE)out, &length) != ERROR_SUCCESS || type != REG_SZ) {
		out[0] = '\0';
		RegCloseKey(handle);
		return 0;
	}
	RegCloseKey(handle);
	out[size - 1] = '\0';
	return out[0] != '\0';
}

static int registryNumber(const char* path, const char* key, DWORD* out) {
	HKEY handle;
	DWORD type = 0;
	DWORD length = sizeof(DWORD);
	DWORD value = 0;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &handle) != ERROR_SUCCESS)
		return 0;
	if (RegQueryValueExA(handle, key, NULL, &type, (LPBYTE)&value, &length) != ERROR_SUCCESS || type != REG_DWORD) {
		RegCloseKey(handle);
		return 0;
	}
	RegCloseKey(handle);
	*out = value;
	return 1;
}

void platIdentity(Info* info) {
	static const char* path = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	char product[128] = "";
	char build[32] = "";
	char user[128] = "";
	char host[128] = "";

	registryString(path, "ProductName", product, sizeof(product));
	registryString(path, "CurrentBuild", build, sizeof(build));

	SYSTEM_INFO system;
	GetNativeSystemInfo(&system);
	const char* arch = "x86_64";
	if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
		arch = "aarch64";
	else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
		arch = "i686";

	if (product[0])
		snprintf(info->os, sizeof(info->os), "%s %s", product, arch);
	else
		snprintf(info->os, sizeof(info->os), "Windows %s", arch);
	copyString(info->kernel, sizeof(info->kernel), build[0] ? build : "NT");

	DWORD size = sizeof(user) - 1;
	GetUserNameA(user, &size);
	if (!user[0])
		copyString(user, sizeof(user), "user");
	copyString(info->user, sizeof(info->user), user);

	size = sizeof(host) - 1;
	GetComputerNameA(host, &size);
	if (!host[0])
		copyString(host, sizeof(host), "localhost");
	copyString(info->host, sizeof(info->host), host);
}

void platEnvironment(Info* info) {
	const char* shell = getenv("SHELL");
	const char* base = shell ? strrchr(shell, '/') : NULL;
	base = base ? base + 1 : shell;
	if (!base || !base[0]) {
		const char* comspec = getenv("COMSPEC");
		base = comspec ? comspec : "cmd.exe";
	}
	copyString(info->shell, sizeof(info->shell), base);

	size_t length = strlen(info->shell);
	if (length > 4 && _stricmp(info->shell + length - 4, ".exe") == 0)
		info->shell[length - 4] = '\0';

	if (getenv("WT_SESSION"))
		copyString(info->terminal, sizeof(info->terminal), "Windows Terminal");
	else {
		const char* program = getenv("TERM_PROGRAM");
		copyString(info->terminal, sizeof(info->terminal), program && program[0] ? program : "console");
	}
	info->wm[0] = '\0';
}

void platUptime(Info* info) {
	fmtUptime((double)GetTickCount64() / 1000.0, info->uptime, sizeof(info->uptime));
}

void platPackages(Info* info) {
	(void)info;
}

static int coreCount(void) {
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = NULL;
	DWORD length = 0;
	int cores = 0;

	GetLogicalProcessorInformation(NULL, &length);
	if (!length)
		return 0;
	info = malloc(length);
	if (!info)
		return 0;
	if (GetLogicalProcessorInformation(info, &length)) {
		DWORD used = length / (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		for (DWORD i = 0; i < used; i++) {
			if (info[i].Relationship == RelationProcessorCore)
				cores++;
		}
	}
	free(info);
	return cores;
}

static long long fileTime(const FILETIME* value) {
	ULARGE_INTEGER number;
	number.LowPart = value->dwLowDateTime;
	number.HighPart = value->dwHighDateTime;
	return (long long)number.QuadPart;
}

static int systemTimes(long long* busy, long long* total) {
	FILETIME idle;
	FILETIME kernel;
	FILETIME user;
	if (!GetSystemTimes(&idle, &kernel, &user))
		return 0;
	*busy = fileTime(&kernel) + fileTime(&user);
	*total = *busy + fileTime(&idle);
	return 1;
}

void platCpu(Info* info, int delayMs) {
	static const char* path = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
	char model[160] = "";
	DWORD mhz = 0;
	SYSTEM_INFO system;

	registryString(path, "ProcessorNameString", model, sizeof(model));
	copyString(info->cpuModel, sizeof(info->cpuModel), model[0] ? model : "unknown");
	registryNumber(path, "MHz", &mhz);

	GetSystemInfo(&system);
	int logical = system.dwNumberOfProcessors;
	int physical = coreCount();
	if (physical <= 0)
		physical = logical;
	if (mhz > 0)
		snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%dC/%dT  %.2f GHz", physical, logical, (double)mhz / 1000.0);
	else
		snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%dC/%dT", physical, logical);

	long long beforeBusy = 0;
	long long beforeTotal = 0;
	long long afterBusy = 0;
	long long afterTotal = 0;
	if (systemTimes(&beforeBusy, &beforeTotal)) {
		Sleep((DWORD)delayMs);
		if (systemTimes(&afterBusy, &afterTotal)) {
			long long busy = afterBusy - beforeBusy;
			long long total = afterTotal - beforeTotal;
			if (total > 0)
				info->cpuPct = (int)(busy * 100 / total);
		}
	}
}

static void adapterName(char* out, size_t size) {
	static const char* path = "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
	HKEY handle;
	char name[32];
	DWORD index = 0;

	out[0] = '\0';
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &handle) != ERROR_SUCCESS)
		return;
	while (RegEnumKeyA(handle, index++, name, sizeof(name)) == ERROR_SUCCESS) {
		char sub[320];
		snprintf(sub, sizeof(sub), "%s\\%s", path, name);
		if (registryString(sub, "HardwareInformation.AdapterString", out, size) && out[0])
			break;
		out[0] = '\0';
	}
	RegCloseKey(handle);
}

static void nvidiaStats(Info* info) {
	FILE* pipe = _popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
	char line[128];
	int busy = -1;
	unsigned long long used = 0;
	unsigned long long total = 0;
	int temp = -1;
	int count;

	if (!pipe)
		return;
	if (!fgets(line, sizeof(line), pipe)) {
		_pclose(pipe);
		return;
	}
	_pclose(pipe);
	count = sscanf(line, "%d, %llu, %llu, %d", &busy, &used, &total, &temp);
	if (count < 3 || total == 0)
		return;
	if (busy >= 0 && info->gpuPct < 0)
		info->gpuPct = busy > 100 ? 100 : busy;
	if (!info->hasVram) {
		fmtPair(used * 1048576ULL, total * 1048576ULL, info->gpuMemText, sizeof(info->gpuMemText));
		info->gpuMemPct = (int)(used * 100 / total);
		info->hasVram = 1;
	}
	if (count == 4 && temp >= 0)
		snprintf(info->gpuTemp, sizeof(info->gpuTemp), "%d\\u00B0C", temp);
}

void platGpu(Info* info) {
	char model[256] = "";

	adapterName(model, sizeof(model));
	if (model[0]) {
		copyString(info->gpuModel, sizeof(info->gpuModel), model);
		info->hasGpu = 1;
	}
	nvidiaStats(info);
}

void platMem(Info* info) {
	MEMORYSTATUSEX state;
	state.dwLength = sizeof(state);
	if (!GlobalMemoryStatusEx(&state))
		return;
	unsigned long long total = state.ullTotalPhys;
	unsigned long long used = total - state.ullAvailPhys;
	if (!total)
		return;
	fmtPair(used, total, info->memText, sizeof(info->memText));
	info->memPct = (int)(used * 100 / total);
}

void platSwap(Info* info) {
	(void)info;
}

void platDisk(Info* info) {
	char drives[512];
	char* drive;
	DWORD length = GetLogicalDriveStringsA((DWORD)sizeof(drives), drives);

	if (length == 0 || length >= sizeof(drives))
		return;
	for (drive = drives; *drive; drive += strlen(drive) + 1) {
		ULARGE_INTEGER freeBytes;
		ULARGE_INTEGER totalBytes;
		ULARGE_INTEGER remainingBytes;
		UINT type = GetDriveTypeA(drive);
		unsigned long long total;
		unsigned long long used;

		if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE)
			continue;
		if (!GetDiskFreeSpaceExA(drive, &remainingBytes, &totalBytes, &freeBytes))
			continue;
		total = (unsigned long long)totalBytes.QuadPart;
		if (!total || total <= (unsigned long long)freeBytes.QuadPart)
			continue;
		used = total - (unsigned long long)freeBytes.QuadPart;
		addDisk(info, drive, used, total);
	}
	sortDisks(info);
}

void platNet(Info* info, int delayMs) {
	(void)info;
	(void)delayMs;
}

void platLoad(Info* info) {
	(void)info;
}

void platBattery(Info* info) {
	SYSTEM_POWER_STATUS status;
	if (!GetSystemPowerStatus(&status))
		return;
	if (status.BatteryFlag == 128)
		return;
	if (status.BatteryLifePercent == 255)
		return;
	if (status.BatteryFlag & 127) {
		char time[32] = "";
		if (status.BatteryLifeTime > 0)
			fmtUptime((double)status.BatteryLifeTime, time, sizeof(time));
		if (status.ACLineStatus == 0 && time[0])
			snprintf(info->battery, sizeof(info->battery), "%u%%  %s left", status.BatteryLifePercent, time);
		else if (status.ACLineStatus == 1)
			snprintf(info->battery, sizeof(info->battery), "%u%%  charging", status.BatteryLifePercent);
		else if (time[0])
			snprintf(info->battery, sizeof(info->battery), "%s", time);
		else
			snprintf(info->battery, sizeof(info->battery), "%u%%", status.BatteryLifePercent);
		info->hasBattery = 1;
	}
}

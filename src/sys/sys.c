#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void copyString(char* dst, size_t size, const char* src) {
	if (!size)
		return;
	snprintf(dst, size, "%s", src ? src : "");
}

int copyValue(const char* line, size_t len, char* out, size_t size) {
	const char* colon = memchr(line, ':', len);
	if (!colon)
		return 0;
	const char* value = colon + 1;
	const char* stop = line + len;
	while (value < stop && (*value == ' ' || *value == '\t'))
		value++;
	size_t count = (size_t)(stop - value);
	if (count >= size)
		count = size - 1;
	memcpy(out, value, count);
	out[count] = '\0';
	strTrim(out);
	return 1;
}

void addDisk(Info* info, const char* mount, unsigned long long used, unsigned long long total) {
	Disk* disk;
	if (!mount || !mount[0] || total == 0 || info->diskCount >= MAX_DISKS)
		return;
	for (int i = 0; i < info->diskCount; i++) {
		if (strcmp(info->disks[i].mount, mount) == 0)
			return;
	}
	disk = &info->disks[info->diskCount];
	snprintf(disk->mount, sizeof(disk->mount), "%s", mount);
	fmtPair(used, total, disk->pair, sizeof(disk->pair));
	disk->pct = (int)(used * 100 / total);
	info->diskCount++;
}

void sortDisks(Info* info) {
	for (int i = 1; i < info->diskCount; i++) {
		Disk key = info->disks[i];
		int j = i - 1;
		while (j >= 0 && strlen(info->disks[j].mount) > strlen(key.mount)) {
			info->disks[j + 1] = info->disks[j];
			j--;
		}
		info->disks[j + 1] = key;
	}
}

int parseOsRelease(const char* text, const char* key, char* out, size_t size) {
	size_t keyLen = strlen(key);
	for (const char* line = text; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > keyLen && strncmp(line, key, keyLen) == 0 && line[keyLen] == '=') {
			const char* value = line + keyLen + 1;
			size_t count = len - keyLen - 1;
			if (count >= 2 && value[0] == '"' && value[count - 1] == '"') {
				value++;
				count -= 2;
			}
			if (count >= size)
				count = size - 1;
			memcpy(out, value, count);
			out[count] = '\0';
			return 1;
		}
		if (!end)
			break;
		line = end + 1;
	}
	return 0;
}

int parseMemInfo(const char* text, const char* key, unsigned long long* outKb) {
	size_t keyLen = strlen(key);
	for (const char* line = text; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > keyLen && strncmp(line, key, keyLen) == 0 && line[keyLen] == ':') {
			const char* value = line + keyLen + 1;
			while (*value == ' ' || *value == '\t')
				value++;
			char* stop = NULL;
			unsigned long long number = strtoull(value, &stop, 10);
			if (stop == value)
				return 0;
			*outKb = number;
			return 1;
		}
		if (!end)
			break;
		line = end + 1;
	}
	return 0;
}

int parseCpuInfo(const char* text, char* model, size_t modelSize, int* threads, int* cores) {
	model[0] = '\0';
	*threads = 0;
	*cores = 0;
	for (const char* line = text; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > 9 && strncmp(line, "processor", 9) == 0) {
			(*threads)++;
		} else if (len > 9 && strncmp(line, "cpu cores", 9) == 0) {
			char value[32] = "";
			if (copyValue(line, len, value, sizeof(value))) {
				int number = atoi(value);
				if (number > *cores)
					*cores = number;
			}
		} else if (model[0] == '\0' && len > 9 &&
			   (strncmp(line, "model name", 10) == 0 ||
			    strncmp(line, "Hardware", 8) == 0 ||
			    strncmp(line, "Model", 5) == 0)) {
			copyValue(line, len, model, modelSize);
		}
		if (!end)
			break;
		line = end + 1;
	}
	return *threads > 0 || model[0] != '\0';
}

int parseCpuFreq(const char* text, double* outGhz) {
	double best = 0;
	for (const char* line = text; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > 7 && strncmp(line, "cpu MHz", 7) == 0) {
			const char* colon = memchr(line, ':', len);
			if (colon) {
				double mhz = atof(colon + 1);
				if (mhz > best)
					best = mhz;
			}
		}
		if (!end)
			break;
		line = end + 1;
	}
	if (best <= 0)
		return 0;
	*outGhz = best / 1000.0;
	return 1;
}

int parseUptime(const char* text, double* outSeconds) {
	char* stop = NULL;
	double seconds = strtod(text, &stop);
	if (stop == text)
		return 0;
	*outSeconds = seconds;
	return 1;
}

int parseLoadAvg(const char* text, char* load, size_t loadSize, char* procs, size_t procsSize) {
	float one = 0;
	float five = 0;
	float fifteen = 0;
	int running = 0;
	int total = 0;
	int count = sscanf(text, "%f %f %f %d/%d", &one, &five, &fifteen, &running, &total);
	if (count < 3)
		return 0;
	snprintf(load, loadSize, "%.2f  %.2f  %.2f", (double)one, (double)five, (double)fifteen);
	if (count == 5)
		snprintf(procs, procsSize, "%d procs", total);
	else
		procs[0] = '\0';
	return 1;
}

int parseStatCpu(const char* text, unsigned long long* busy, unsigned long long* total) {
	if (strncmp(text, "cpu ", 4) != 0)
		return 0;
	unsigned long long values[10] = { 0 };
	const char* p = text + 4;
	int count = 0;
	while (count < 10) {
		char* stop = NULL;
		unsigned long long value = strtoull(p, &stop, 10);
		if (stop == p)
			break;
		values[count++] = value;
		p = stop;
	}
	if (count < 4)
		return 0;
	unsigned long long sum = 0;
	for (int i = 0; i < count; i++)
		sum += values[i];
	unsigned long long idle = values[3] + values[4];
	*total = sum;
	*busy = sum - idle;
	return 1;
}

int parseNetDev(const char* text, const char* iface, unsigned long long* rx, unsigned long long* tx) {
	for (const char* line = text; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		const char* colon = memchr(line, ':', len);
		if (colon) {
			const char* start = line;
			while (start < colon && (*start == ' ' || *start == '\t'))
				start++;
			size_t nameLen = (size_t)(colon - start);
			if (nameLen < 64 && strncmp(start, iface, nameLen) == 0 && iface[nameLen] == '\0') {
				const char* p = colon + 1;
				unsigned long long values[16] = { 0 };
				int count = 0;
				while (count < 16) {
					char* stop = NULL;
					unsigned long long value = strtoull(p, &stop, 10);
					if (stop == p)
						break;
					values[count++] = value;
					p = stop;
				}
				if (count < 9)
					return 0;
				*rx = values[0];
				*tx = values[8];
				return 1;
			}
		}
		if (!end)
			break;
		line = end + 1;
	}
	return 0;
}

int parseMilliC(const char* text, int* outCelsius) {
	char* stop = NULL;
	long value = strtol(text, &stop, 10);
	if (stop == text)
		return 0;
	*outCelsius = (int)(value / 1000);
	return 1;
}

void getInfo(Info* info, int delayMs) {
	memset(info, 0, sizeof(*info));
	info->cpuPct = -1;
	info->gpuPct = -1;
	info->gpuMemPct = -1;
	info->memPct = -1;
	info->swapPct = -1;
	platIdentity(info);
	platEnvironment(info);
	platUptime(info);
	platPackages(info);
	platCpu(info, delayMs);
	platGpu(info);
	platMem(info);
	platSwap(info);
	platDisk(info);
	platNet(info, delayMs);
	platLoad(info);
	platBattery(info);
}

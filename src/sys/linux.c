#include "plat.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>


static void removeSub(char* text, const char* sub) {
	size_t subLen = strlen(sub);
	char* at;
	while ((at = strstr(text, sub)) != NULL)
		memmove(at, at + subLen, strlen(at + subLen) + 1);
}

static int isCpuNoise(const char* token) {
	if (strcmp(token, "AMD") == 0 || strcmp(token, "Intel") == 0 ||
	    strcmp(token, "Processor") == 0 || strcmp(token, "CPU") == 0)
		return 1;
	size_t len = strlen(token);
	if (len > 5 && strcmp(token + len - 5, "-Core") == 0 &&
	    token[0] >= '0' && token[0] <= '9')
		return 1;
	return 0;
}

static void cleanCpu(char* text, size_t size) {
	removeSub(text, "(R)");
	removeSub(text, "(TM)");
	removeSub(text, "(r)");
	removeSub(text, "(tm)");
	char rebuilt[256] = "";
	char* save = NULL;
	for (char* token = strtok_r(text, " \t", &save); token; token = strtok_r(NULL, " \t", &save)) {
		if (isCpuNoise(token))
			continue;
		if (rebuilt[0])
			strncat(rebuilt, " ", sizeof(rebuilt) - strlen(rebuilt) - 1);
		strncat(rebuilt, token, sizeof(rebuilt) - strlen(rebuilt) - 1);
	}
	strTrim(rebuilt);
	copyString(text, size, rebuilt[0] ? rebuilt : text);
}

static void cleanGpu(char* out, size_t size, const char* raw) {
	const char* open = strchr(raw, '[');
	const char* close = open ? strchr(open, ']') : NULL;
	if (open && close && close > open + 1) {
		size_t count = (size_t)(close - open - 1);
		if (count >= size)
			count = size - 1;
		memcpy(out, open + 1, count);
		out[count] = '\0';
		return;
	}
	copyString(out, size, raw);
}

static int countDirs(const char* path) {
	DIR* dir = opendir(path);
	if (!dir)
		return 0;
	int count = 0;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		count++;
	}
	closedir(dir);
	return count;
}

static int countDpkg(void) {
	char* text = readFileAlloc("/var/lib/dpkg/status", NULL);
	if (!text)
		return 0;
	static const char* marker = "Status: install ok installed";
	int count = 0;
	for (const char* p = text; (p = strstr(p, marker)); p += strlen(marker))
		count++;
	free(text);
	return count;
}

static int defaultIface(char* out, size_t size) {
	char* text = readFileAlloc("/proc/net/route", NULL);
	if (!text)
		return 0;
	int found = 0;
	char* line = text;
	while (line && *line) {
		char* newline = strchr(line, '\n');
		if (newline)
			*newline = '\0';
		char iface[64];
		char dest[32];
		if (sscanf(line, "%63s %31s", iface, dest) == 2 && strcmp(dest, "00000000") == 0) {
			copyString(out, size, iface);
			found = 1;
			break;
		}
		line = newline ? newline + 1 : NULL;
	}
	free(text);
	return found;
}

static int ifaceIp(const char* iface, char* out, size_t size) {
	struct ifaddrs* addrs = NULL;
	if (getifaddrs(&addrs) != 0)
		return 0;
	int found = 0;
	for (struct ifaddrs* it = addrs; it; it = it->ifa_next) {
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
			continue;
		if (strcmp(it->ifa_name, iface) != 0)
			continue;
		struct sockaddr_in* address = (struct sockaddr_in*)it->ifa_addr;
		inet_ntop(AF_INET, &address->sin_addr, out, (socklen_t)size);
		found = 1;
		break;
	}
	freeifaddrs(addrs);
	return found;
}

static int hwmonTemp(const char* wanted) {
	DIR* dir = opendir("/sys/class/hwmon");
	if (!dir)
		return -1;
	int result = -1;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
		char name[64] = "";
		if (readTrimLine(path, name, sizeof(name)) != 0)
			continue;
		if (strcmp(name, wanted) != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", entry->d_name);
		char value[32] = "";
		if (readTrimLine(path, value, sizeof(value)) != 0)
			continue;
		int celsius = 0;
		if (parseMilliC(value, &celsius)) {
			result = celsius;
			break;
		}
	}
	closedir(dir);
	return result;
}

static int thermalTemp(void) {
	DIR* dir = opendir("/sys/class/thermal");
	if (!dir)
		return -1;
	int result = -1;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, "thermal_zone", 12) != 0)
			continue;
		char path[512];
		snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
		char value[32] = "";
		if (readTrimLine(path, value, sizeof(value)) != 0)
			continue;
		int celsius = 0;
		if (parseMilliC(value, &celsius) && celsius > 0 && celsius < 150) {
			result = celsius;
			break;
		}
	}
	closedir(dir);
	return result;
}

static int cpuTemp(void) {
	static const char* names[] = { "k10temp", "zenpower", "coretemp", "cpu_thermal", "acpitz" };
	for (size_t i = 0; i < ARRAY_LEN(names); i++) {
		int celsius = hwmonTemp(names[i]);
		if (celsius >= 0)
			return celsius;
	}
	return thermalTemp();
}

static int gpuTemp(void) {
	static const char* names[] = { "amdgpu", "nouveau", "radeon" };
	for (size_t i = 0; i < ARRAY_LEN(names); i++) {
		int celsius = hwmonTemp(names[i]);
		if (celsius >= 0)
			return celsius;
	}
	return -1;
}

static int gpuBusy(void) {
	DIR* dir = opendir("/sys/class/drm");
	if (!dir)
		return -1;
	int result = -1;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, "card", 4) != 0)
			continue;
		char path[512];
		snprintf(path, sizeof(path), "/sys/class/drm/%s/device/gpu_busy_percent", entry->d_name);
		char value[32] = "";
		if (readTrimLine(path, value, sizeof(value)) == 0) {
			result = atoi(value);
			break;
		}
	}
	closedir(dir);
	return result;
}

static int isShell(const char* name) {
	static const char* shells[] = {
		"sh", "bash", "zsh", "fish", "dash", "ksh", "csh", "tcsh",
		"nu", "elvish", "xonsh", "neomon"
	};
	for (size_t i = 0; i < ARRAY_LEN(shells); i++) {
		if (strcmp(name, shells[i]) == 0)
			return 1;
	}
	return 0;
}

static pid_t parentPid(pid_t pid) {
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	char text[1024];
	if (readTrimLine(path, text, sizeof(text)) != 0)
		return 0;
	char* close = strrchr(text, ')');
	if (!close)
		return 0;
	char* p = close + 1;
	while (*p == ' ')
		p++;
	while (*p && *p != ' ')
		p++;
	while (*p == ' ')
		p++;
	return (pid_t)atoi(p);
}

static void getOs(Info* info) {
	Buf buf;
	bufInit(&buf);
	char name[128] = "";
	if (readFileInto("/etc/os-release", &buf) == 0)
		parseOsRelease(buf.data, "NAME", name, sizeof(name));
	if (!name[0])
		copyString(name, sizeof(name), "Linux");
	struct utsname uts;
	if (uname(&uts) == 0) {
		snprintf(info->os, sizeof(info->os), "%s %s", name, uts.machine);
		copyString(info->kernel, sizeof(info->kernel), uts.release);
	} else {
		copyString(info->os, sizeof(info->os), name);
		copyString(info->kernel, sizeof(info->kernel), "n/a");
	}
	bufFree(&buf);
}

static void getUser(Info* info) {
	const char* user = getenv("USER");
	if (!user || !user[0]) {
		struct passwd* entry = getpwuid(getuid());
		user = entry ? entry->pw_name : "user";
	}
	copyString(info->user, sizeof(info->user), user);
	char host[128] = "";
	if (gethostname(host, sizeof(host) - 1) != 0)
		host[0] = '\0';
	host[sizeof(host) - 1] = '\0';
	copyString(info->host, sizeof(info->host), host[0] ? host : "localhost");
}

static void getShell(Info* info) {
	const char* shell = getenv("SHELL");
	const char* base = shell ? strrchr(shell, '/') : NULL;
	base = base ? base + 1 : shell;
	copyString(info->shell, sizeof(info->shell), base ? base : "sh");
}

static void getWm(Info* info) {
	const char* wm = getenv("XDG_CURRENT_DESKTOP");
	if (!wm || !wm[0])
		wm = getenv("XDG_SESSION_DESKTOP");
	if (!wm || !wm[0])
		wm = getenv("DESKTOP_SESSION");
	if (wm && wm[0]) {
		copyString(info->wm, sizeof(info->wm), wm);
		for (char* p = info->wm; *p; p++) {
			if (*p == ':')
				*p = ' ';
		}
		return;
	}
	if (getenv("WAYLAND_DISPLAY"))
		copyString(info->wm, sizeof(info->wm), "Wayland");
	else if (getenv("DISPLAY"))
		copyString(info->wm, sizeof(info->wm), "X11");
	else
		copyString(info->wm, sizeof(info->wm), "tty");
}

static void getTerminal(Info* info) {
	char found[64] = "";
	pid_t pid = getppid();
	for (int depth = 0; depth < 8 && pid > 1; depth++) {
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
		char comm[64] = "";
		if (readTrimLine(path, comm, sizeof(comm)) != 0)
			break;
		if (!isShell(comm)) {
			copyString(found, sizeof(found), comm);
			break;
		}
		pid = parentPid(pid);
	}
	if (!found[0]) {
		const char* program = getenv("TERM_PROGRAM");
		if (program && program[0])
			copyString(found, sizeof(found), program);
	}
	copyString(info->terminal, sizeof(info->terminal), found[0] ? found : "tty");
}

static void getUptime(Info* info) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto("/proc/uptime", &buf) == 0) {
		double seconds = 0;
		if (parseUptime(buf.data, &seconds))
			fmtUptime(seconds, info->uptime, sizeof(info->uptime));
	}
	if (!info->uptime[0])
		copyString(info->uptime, sizeof(info->uptime), "n/a");
	bufFree(&buf);
}

static void getPackages(Info* info) {
	Buf parts;
	bufInit(&parts);
	int pacman = countDirs("/var/lib/pacman/local");
	int dpkg = countDpkg();
	int flatpak = countDirs("/var/lib/flatpak/app");
	const char* home = getenv("HOME");
	if (home && home[0]) {
		char path[512];
		snprintf(path, sizeof(path), "%s/.local/share/flatpak/app", home);
		flatpak += countDirs(path);
	}
	if (pacman)
		bufAddF(&parts, "%d pacman", pacman);
	if (dpkg) {
		if (parts.len)
			bufAdd(&parts, "  ");
		bufAddF(&parts, "%d dpkg", dpkg);
	}
	if (flatpak) {
		if (parts.len)
			bufAdd(&parts, "  ");
		bufAddF(&parts, "%d flatpak", flatpak);
	}
	copyString(info->packages, sizeof(info->packages), parts.len ? parts.data : "n/a");
	bufFree(&parts);
}

static void getCpu(Info* info, int delayMs) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto("/proc/cpuinfo", &buf) == 0) {
		int threads = 0;
		int cores = 0;
		parseCpuInfo(buf.data, info->cpuModel, sizeof(info->cpuModel), &threads, &cores);
		cleanCpu(info->cpuModel, sizeof(info->cpuModel));
		if (cores <= 0)
			cores = threads;
		double ghz = 0;
		if (!parseCpuFreq(buf.data, &ghz)) {
			char line[32] = "";
			if (readTrimLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", line, sizeof(line)) == 0)
				ghz = atof(line) / 1e6;
		}
		if (ghz > 0)
			snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%dC/%dT  %.2f GHz", cores, threads, ghz);
		else
			snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%dC/%dT", cores, threads);
	}
	if (!info->cpuModel[0])
		copyString(info->cpuModel, sizeof(info->cpuModel), "CPU");
	bufFree(&buf);

	Buf before;
	Buf after;
	bufInit(&before);
	bufInit(&after);
	if (readFileInto("/proc/stat", &before) == 0) {
		usleep((useconds_t)delayMs * 1000);
		if (readFileInto("/proc/stat", &after) == 0) {
			unsigned long long busy1 = 0;
			unsigned long long total1 = 0;
			unsigned long long busy2 = 0;
			unsigned long long total2 = 0;
			if (parseStatCpu(before.data, &busy1, &total1) && parseStatCpu(after.data, &busy2, &total2)) {
				unsigned long long busy = busy2 - busy1;
				unsigned long long total = total2 - total1;
				if (total > 0)
					info->cpuPct = (int)(busy * 100 / total);
			}
		}
	}
	bufFree(&before);
	bufFree(&after);

	int celsius = cpuTemp();
	if (celsius >= 0)
		snprintf(info->cpuTemp, sizeof(info->cpuTemp), "%d\u00B0C", celsius);
}

static void getNvidia(Info* info) {
	FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
	if (!pipe)
		return;
	char line[128];
	if (fgets(line, sizeof(line), pipe)) {
		int busy = -1;
		unsigned long long used = 0;
		unsigned long long total = 0;
		int temp = -1;
		int count = sscanf(line, "%d, %llu, %llu, %d", &busy, &used, &total, &temp);
		if (count >= 3 && total > 0) {
			if (busy >= 0 && info->gpuPct < 0)
				info->gpuPct = busy > 100 ? 100 : busy;
			if (!info->hasVram) {
				fmtPair(used * 1048576ULL, total * 1048576ULL, info->gpuMemText, sizeof(info->gpuMemText));
				info->gpuMemPct = (int)(used * 100 / total);
				info->hasVram = 1;
			}
			if (count == 4 && temp >= 0 && !info->gpuTemp[0])
				snprintf(info->gpuTemp, sizeof(info->gpuTemp), "%d\u00B0C", temp);
		}
	}
	pclose(pipe);
}

static int vramSysfs(unsigned long long* used, unsigned long long* total) {
	DIR* dir = opendir("/sys/class/drm");
	if (!dir)
		return 0;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, "card", 4) != 0)
			continue;
		char path[512];
		char value[32] = "";
		snprintf(path, sizeof(path), "/sys/class/drm/%s/device/mem_info_vram_total", entry->d_name);
		if (readTrimLine(path, value, sizeof(value)) != 0)
			continue;
		char* stop = NULL;
		unsigned long long size = strtoull(value, &stop, 10);
		if (stop == value || size == 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/drm/%s/device/mem_info_vram_used", entry->d_name);
		if (readTrimLine(path, value, sizeof(value)) != 0)
			continue;
		*total = size;
		*used = strtoull(value, &stop, 10);
		closedir(dir);
		return 1;
	}
	closedir(dir);
	return 0;
}

static void getGpu(Info* info) {
	FILE* pipe = popen("lspci 2>/dev/null", "r");
	if (pipe) {
		char line[512];
		while (fgets(line, sizeof(line), pipe)) {
			if (!strstr(line, "VGA compatible controller") &&
			    !strstr(line, "3D controller") &&
			    !strstr(line, "Display controller"))
				continue;
			char* text = strstr(line, "controller: ");
			if (text)
				text += strlen("controller: ");
			else {
				text = strchr(line, ':');
				if (text)
					text++;
			}
			if (!text)
				continue;
			char* rev = strstr(text, " (rev");
			if (rev)
				*rev = '\0';
			strTrim(text);
			cleanGpu(info->gpuModel, sizeof(info->gpuModel), text);
			info->hasGpu = 1;
			break;
		}
		pclose(pipe);
	}
	int busy = gpuBusy();
	if (busy >= 0)
		info->gpuPct = busy;
	int celsius = gpuTemp();
	if (celsius >= 0)
		snprintf(info->gpuTemp, sizeof(info->gpuTemp), "%d\u00B0C", celsius);
	getNvidia(info);
	if (!info->hasVram) {
		unsigned long long used = 0;
		unsigned long long total = 0;
		if (vramSysfs(&used, &total) && total > 0) {
			fmtPair(used, total, info->gpuMemText, sizeof(info->gpuMemText));
			info->gpuMemPct = (int)(used * 100 / total);
			info->hasVram = 1;
		}
	}
}

static void getMem(Info* info) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto("/proc/meminfo", &buf) == 0) {
		unsigned long long total = 0;
		unsigned long long available = 0;
		if (parseMemInfo(buf.data, "MemTotal", &total) && parseMemInfo(buf.data, "MemAvailable", &available)) {
			unsigned long long used = total - available;
			fmtPair(used * 1024, total * 1024, info->memText, sizeof(info->memText));
			if (total > 0)
				info->memPct = (int)(used * 100 / total);
		}
	}
	bufFree(&buf);
}

static void getSwap(Info* info) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto("/proc/meminfo", &buf) == 0) {
		unsigned long long total = 0;
		unsigned long long freeKb = 0;
		if (parseMemInfo(buf.data, "SwapTotal", &total) && parseMemInfo(buf.data, "SwapFree", &freeKb) && total > 0) {
			unsigned long long used = total - freeKb;
			fmtPair(used * 1024, total * 1024, info->swapText, sizeof(info->swapText));
			info->swapPct = (int)(used * 100 / total);
			info->hasSwap = 1;
		}
	}
	bufFree(&buf);
}

static int realFs(const char* type) {
	static const char* keep[] = {
		"ext2", "ext3", "ext4", "xfs", "btrfs", "f2fs", "jfs", "reiserfs",
		"nilfs2", "erofs", "zfs", "ocfs2", "bcachefs", "ntfs", "ntfs3",
		"vfat", "exfat", "udf", "hfsplus", "apfs", "fuseblk"
	};
	for (size_t i = 0; i < ARRAY_LEN(keep); i++) {
		if (strcmp(type, keep[i]) == 0)
			return 1;
	}
	return 0;
}

static int mountUsable(const char* mount) {
	static const char* skip[] = { "/proc", "/sys", "/dev", "/run", "/snap", "/boot" };
	for (size_t i = 0; i < ARRAY_LEN(skip); i++) {
		if (strncmp(mount, skip[i], strlen(skip[i])) == 0)
			return 0;
	}
	return 1;
}

static void getDisk(Info* info) {
	char* text = readFileAlloc("/proc/mounts", NULL);
	char* line;
	if (!text)
		return;
	line = text;
	while (line && *line) {
		char* newline = strchr(line, '\n');
		char device[256];
		char mount[256];
		char type[64];
		struct statvfs vfs;
		unsigned long long block;
		unsigned long long total;
		unsigned long long free;
		unsigned long long used;

		if (newline)
			*newline = '\0';
		if (sscanf(line, "%255s %255s %63s", device, mount, type) == 3 &&
		    realFs(type) && mountUsable(mount) && statvfs(mount, &vfs) == 0) {
			block = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
			total = (unsigned long long)vfs.f_blocks * block;
			free = (unsigned long long)vfs.f_bfree * block;
			if (block && total > free) {
				used = total - free;
				addDisk(info, mount, used, total);
			}
		}
		line = newline ? newline + 1 : NULL;
	}
	free(text);
	sortDisks(info);
}

static void getNet(Info* info, int delayMs) {
	char iface[64] = "";
	if (!defaultIface(iface, sizeof(iface))) {
		if (ifaceIp("wlan0", info->netIp, sizeof(info->netIp)))
			copyString(iface, sizeof(iface), "wlan0");
		else if (ifaceIp("eth0", info->netIp, sizeof(info->netIp)))
			copyString(iface, sizeof(iface), "eth0");
	}
	if (!iface[0])
		return;
	copyString(info->netName, sizeof(info->netName), iface);
	if (!info->netIp[0])
		ifaceIp(iface, info->netIp, sizeof(info->netIp));

	Buf before;
	Buf after;
	bufInit(&before);
	bufInit(&after);
	if (readFileInto("/proc/net/dev", &before) == 0) {
		usleep((useconds_t)delayMs * 1000);
		if (readFileInto("/proc/net/dev", &after) == 0) {
			unsigned long long rx1 = 0;
			unsigned long long tx1 = 0;
			unsigned long long rx2 = 0;
			unsigned long long tx2 = 0;
			if (parseNetDev(before.data, iface, &rx1, &tx1) && parseNetDev(after.data, iface, &rx2, &tx2)) {
				double seconds = (double)delayMs / 1000.0;
				fmtRate(rx2 - rx1, seconds, info->netDown, sizeof(info->netDown));
				fmtRate(tx2 - tx1, seconds, info->netUp, sizeof(info->netUp));
			}
		}
	}
	bufFree(&before);
	bufFree(&after);
	info->hasNet = 1;
}

static void getLoad(Info* info) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto("/proc/loadavg", &buf) == 0)
		parseLoadAvg(buf.data, info->load, sizeof(info->load), info->procs, sizeof(info->procs));
	bufFree(&buf);
}

static void getBattery(Info* info) {
	DIR* dir = opendir("/sys/class/power_supply");
	if (!dir)
		return;
	struct dirent* entry;
	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, "BAT", 3) != 0)
			continue;
		char path[512];
		char capacity[32] = "";
		char status[32] = "";
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", entry->d_name);
		readTrimLine(path, capacity, sizeof(capacity));
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", entry->d_name);
		readTrimLine(path, status, sizeof(status));
		if (!capacity[0])
			continue;
		if (strcmp(status, "Charging") == 0) {
			snprintf(info->battery, sizeof(info->battery), "%s%%  charging", capacity);
		} else if (strcmp(status, "Discharging") == 0) {
			char now[32] = "";
			char full[32] = "";
			char rate[32] = "";
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_now", entry->d_name);
			readTrimLine(path, now, sizeof(now));
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_full", entry->d_name);
			readTrimLine(path, full, sizeof(full));
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/power_now", entry->d_name);
			readTrimLine(path, rate, sizeof(rate));
			double power = atof(rate);
			if (power > 0) {
				char time[32];
				fmtUptime(atof(now) / power * 3600.0, time, sizeof(time));
				snprintf(info->battery, sizeof(info->battery), "%s%%  %s left", capacity, time);
			} else {
				snprintf(info->battery, sizeof(info->battery), "%s%%  discharging", capacity);
			}
		} else {
			snprintf(info->battery, sizeof(info->battery), "%s%%", capacity);
		}
		info->hasBattery = 1;
		break;
	}
	closedir(dir);
}


void platIdentity(Info* info) {
	getOs(info);
	getUser(info);
}

void platEnvironment(Info* info) {
	getShell(info);
	getWm(info);
	getTerminal(info);
}

void platUptime(Info* info) {
	getUptime(info);
}

void platPackages(Info* info) {
	getPackages(info);
}

void platCpu(Info* info, int delayMs) {
	getCpu(info, delayMs);
}

void platGpu(Info* info) {
	getGpu(info);
}

void platMem(Info* info) {
	getMem(info);
}

void platSwap(Info* info) {
	getSwap(info);
}

void platDisk(Info* info) {
	getDisk(info);
}

void platNet(Info* info, int delayMs) {
	getNet(info, delayMs);
}

void platLoad(Info* info) {
	getLoad(info);
}

void platBattery(Info* info) {
	getBattery(info);
}


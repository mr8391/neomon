#include "plat.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

static int cpuTicks(long long* busy, long long* total);

static int sysString(const char* name, char* out, size_t size) {
	char buffer[512];
	size_t length = sizeof(buffer);
	if (sysctlbyname(name, buffer, &length, NULL, 0) != 0)
		return 0;
	if (length >= size)
		length = size - 1;
	memcpy(out, buffer, length);
	out[length] = '\0';
	return 1;
}

static int sysNumber(const char* name, unsigned long long* out) {
	unsigned long long value = 0;
	size_t length = sizeof(value);
	if (sysctlbyname(name, &value, &length, NULL, 0) != 0)
		return 0;
	*out = value;
	return 1;
}

void platIdentity(Info* info) {
	char release[64] = "";
	char product[64] = "";
	struct utsname uts;

	if (uname(&uts) == 0)
		copyString(info->kernel, sizeof(info->kernel), uts.release);
	else
		copyString(info->kernel, sizeof(info->kernel), "Darwin");

	sysString("kern.osproductversion", product, sizeof(product));
	if (!product[0])
		sysString("kern.osrelease", release, sizeof(release));
	snprintf(info->os, sizeof(info->os), "macOS %s", product[0] ? product : release);

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

void platEnvironment(Info* info) {
	const char* shell = getenv("SHELL");
	const char* base = shell ? strrchr(shell, '/') : NULL;
	base = base ? base + 1 : shell;
	copyString(info->shell, sizeof(info->shell), base ? base : "zsh");

	const char* program = getenv("TERM_PROGRAM");
	if (program && program[0])
		copyString(info->terminal, sizeof(info->terminal), program);
	else
		copyString(info->terminal, sizeof(info->terminal), "Terminal");

	info->wm[0] = '\0';
}

void platUptime(Info* info) {
	struct timeval boot;
	size_t length = sizeof(boot);
	if (sysctlbyname("kern.boottime", &boot, &length, NULL, 0) != 0) {
		copyString(info->uptime, sizeof(info->uptime), "n/a");
		return;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	double seconds = (double)(now.tv_sec - boot.tv_sec);
	if (seconds < 0)
		seconds = 0;
	fmtUptime(seconds, info->uptime, sizeof(info->uptime));
}

void platPackages(Info* info) {
	DIR* dir;
	struct dirent* entry;
	int homebrew = 0;
	static const char* prefixes[] = { "/opt/homebrew/Cellar", "/usr/local/Cellar" };

	for (size_t i = 0; i < ARRAY_LEN(prefixes); i++) {
		dir = opendir(prefixes[i]);
		if (!dir)
			continue;
		while ((entry = readdir(dir))) {
			if (entry->d_name[0] != '.')
				homebrew++;
		}
		closedir(dir);
	}
	if (homebrew > 0)
		snprintf(info->packages, sizeof(info->packages), "%d homebrew", homebrew);
}

void platCpu(Info* info, int delayMs) {
	char brand[160] = "";
	unsigned long long logical = 0;
	unsigned long long physical = 0;
	unsigned long long hz = 0;

	sysString("machdep.cpu.brand_string", brand, sizeof(brand));
	if (!brand[0])
		sysString("hw.model", brand, sizeof(brand));
	copyString(info->cpuModel, sizeof(info->cpuModel), brand[0] ? brand : "unknown");

	sysNumber("hw.logicalcpu", &logical);
	sysNumber("hw.physicalcpu", &physical);
	if (physical == 0)
		physical = logical;
	if (sysNumber("hw.cpufrequency", &hz) && hz > 0)
		snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%lluC/%lluT  %.2f GHz", physical, logical, (double)hz / 1e9);
	else
		snprintf(info->cpuSpec, sizeof(info->cpuSpec), "%lluC/%lluT", physical, logical);

	long long beforeBusy = 0;
	long long beforeTotal = 0;
	long long afterBusy = 0;
	long long afterTotal = 0;
	if (cpuTicks(&beforeBusy, &beforeTotal)) {
		usleep((useconds_t)delayMs * 1000);
		if (cpuTicks(&afterBusy, &afterTotal)) {
			long long busy = afterBusy - beforeBusy;
			long long total = afterTotal - beforeTotal;
			if (total > 0)
				info->cpuPct = (int)(busy * 100 / total);
		}
	}

}

static int cpuTicks(long long* busy, long long* total) {
	natural_t cpuCount = 0;
	processor_info_array_t array = NULL;
	mach_msg_type_number_t count = 0;
	*busy = 0;
	*total = 0;
	if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &array, &count) != KERN_SUCCESS)
		return 0;
	int* ticks = (int*)array;
	for (natural_t i = 0; i < cpuCount; i++) {
		int base = (int)i * CPU_STATE_MAX;
		long long user = (unsigned int)ticks[base + CPU_STATE_USER];
		long long system = (unsigned int)ticks[base + CPU_STATE_SYSTEM];
		long long idle = (unsigned int)ticks[base + CPU_STATE_IDLE];
		long long nice = (unsigned int)ticks[base + CPU_STATE_NICE];
		*busy += user + system + nice;
		*total += user + system + nice + idle;
	}
	vm_deallocate(mach_task_self(), (vm_address_t)array, count * sizeof(int));
	return 1;
}

void platGpu(Info* info) {
	(void)info;
}

void platMem(Info* info) {
	unsigned long long total = 0;
	if (!sysNumber("hw.memsize", &total) || total == 0)
		return;
	vm_statistics64_data_t stats;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&stats, &count) != KERN_SUCCESS)
		return;
	unsigned long long pagesize = (unsigned long long)getpagesize();
	unsigned long long free = (unsigned long long)(stats.free_count + stats.inactive_count + stats.speculative_count) * pagesize;
	if (free > total)
		free = 0;
	unsigned long long used = total - free;
	fmtPair(used, total, info->memText, sizeof(info->memText));
	info->memPct = (int)(used * 100 / total);
}

void platSwap(Info* info) {
	(void)info;
}

static int appleFs(const char* type) {
	static const char* keep[] = { "apfs", "hfs", "hfs+", "msdos", "exfat", "ntfs", "ufs", "smbfs" };
	for (size_t i = 0; i < ARRAY_LEN(keep); i++) {
		if (strcmp(type, keep[i]) == 0)
			return 1;
	}
	return 0;
}

void platDisk(Info* info) {
	struct statfs* mounts;
	int count;
	int haveData = 0;

	count = getfsstat(NULL, 0, MNT_NOWAIT);
	if (count <= 0)
		return;
	mounts = malloc(sizeof(struct statfs) * (size_t)count);
	if (!mounts)
		return;
	count = getfsstat(mounts, (int)(sizeof(struct statfs) * (size_t)count), MNT_NOWAIT);
	for (int i = 0; i < count; i++) {
		if (strcmp(mounts[i].f_mntonname, "/System/Volumes/Data") == 0)
			haveData = 1;
	}
	for (int i = 0; i < count; i++) {
		const char* mount = mounts[i].f_mntonname;
		const char* label;
		unsigned long long block;
		unsigned long long total;
		unsigned long long free;
		unsigned long long used;

		if (!appleFs(mounts[i].f_fstypename))
			continue;
		if (strncmp(mount, "/System/Volumes/", 17) == 0) {
			if (strcmp(mount, "/System/Volumes/Data") != 0)
				continue;
			label = "/";
		} else if (strcmp(mount, "/") == 0) {
			if (haveData)
				continue;
			label = "/";
		} else if (strncmp(mount, "/private/", 9) == 0) {
			continue;
		} else {
			label = mount;
		}
		block = (unsigned long long)mounts[i].f_bsize;
		total = (unsigned long long)mounts[i].f_blocks * block;
		free = (unsigned long long)mounts[i].f_bfree * block;
		if (!block || total <= free)
			continue;
		used = total - free;
		addDisk(info, label, used, total);
	}
	free(mounts);
	sortDisks(info);
}

static int ifaceCounters(const char* want, unsigned long long* rx, unsigned long long* tx) {
	struct ifaddrs* addrs = NULL;
	if (getifaddrs(&addrs) != 0)
		return 0;
	int found = 0;
	for (struct ifaddrs* it = addrs; it; it = it->ifa_next) {
		if (!it->ifa_name || strcmp(it->ifa_name, want) != 0)
			continue;
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_LINK)
			continue;
		struct if_data* data = (struct if_data*)it->ifa_data;
		if (!data)
			continue;
		*rx = (unsigned long long)data->ifi_ibytes;
		*tx = (unsigned long long)data->ifi_obytes;
		found = 1;
		break;
	}
	freeifaddrs(addrs);
	return found;
}

static void pickIface(char* out, size_t size) {
	struct ifaddrs* addrs = NULL;
	copyString(out, size, "");
	if (getifaddrs(&addrs) != 0)
		return;
	for (struct ifaddrs* it = addrs; it; it = it->ifa_next) {
		if (!it->ifa_name || strcmp(it->ifa_name, "lo0") == 0)
			continue;
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
			continue;
		copyString(out, size, it->ifa_name);
		if (strcmp(it->ifa_name, "en0") == 0)
			break;
	}
	freeifaddrs(addrs);
}

void platNet(Info* info, int delayMs) {
	char iface[32] = "";
	struct ifaddrs* addrs = NULL;
	unsigned long long rx1 = 0;
	unsigned long long tx1 = 0;
	unsigned long long rx2 = 0;
	unsigned long long tx2 = 0;

	pickIface(iface, sizeof(iface));
	if (!iface[0])
		return;

	if (getifaddrs(&addrs) == 0) {
		for (struct ifaddrs* it = addrs; it; it = it->ifa_next) {
			if (!it->ifa_name || strcmp(it->ifa_name, iface) != 0)
				continue;
			if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
				continue;
			struct sockaddr_in* address = (struct sockaddr_in*)it->ifa_addr;
			inet_ntop(AF_INET, &address->sin_addr, info->netIp, (socklen_t)sizeof(info->netIp));
			break;
		}
		freeifaddrs(addrs);
	}

	if (!ifaceCounters(iface, &rx1, &tx1))
		return;
	usleep((useconds_t)delayMs * 1000);
	if (!ifaceCounters(iface, &rx2, &tx2))
		return;
	double seconds = (double)delayMs / 1000.0;
	copyString(info->netName, sizeof(info->netName), iface);
	fmtRate(rx2 - rx1, seconds, info->netDown, sizeof(info->netDown));
	fmtRate(tx2 - tx1, seconds, info->netUp, sizeof(info->netUp));
	info->hasNet = 1;
}

void platLoad(Info* info) {
	double values[3] = { 0, 0, 0 };
	int count = getloadavg(values, 3);
	if (count != 3)
		return;
	int total = 0;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t length = 0;
	if (sysctl(mib, 4, NULL, &length, NULL, 0) == 0)
		total = (int)(length / sizeof(struct kinfo_proc));
	if (total > 0) {
		snprintf(info->load, sizeof(info->load), "%.2f  %.2f  %.2f", values[0], values[1], values[2]);
		snprintf(info->procs, sizeof(info->procs), "%d procs", total);
	}
}

void platBattery(Info* info) {
	(void)info;
}

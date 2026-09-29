#include "core/core.h"
#include "image/image.h"
#include "logo/logo.h"
#include "render/render.h"
#include "sys/sys.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "0.1.0"
#define LIVE_INTERVAL_MS 400

static volatile sig_atomic_t gRunning = 1;

static void stopLive(int signo) {
	(void)signo;
	gRunning = 0;
}

static void printHelp(void) {
	puts("neomon - system monitor fetch");
	puts("");
	puts("usage: neomon [options]");
	puts("");
	puts("  -L, --logo NAME   force a distro logo by name");
	puts("      --big         use the full size logo (default)");
	puts("      --small       use the compact logo");
	puts("      --delay MS    sampling window for cpu and net, default 100");
	puts("      --no-color    disable colors");
	puts("      --no-icons    disable label icons");
	puts("      --live        refresh usage in real time until Ctrl+C");
	puts("      --image       use a PNG logo when the terminal renders images");
	puts("      --no-image    never use a PNG logo");
	puts("      --list        list known logo names");
	puts("  -h, --help        show this help");
	puts("      --version     show the version");
}

static void useSmall(LogoTarget* target) {
	char name[160];
	snprintf(name, sizeof(name), "%s_small", target->file);
	LogoTarget small;
	if (logoLookup(name, &small))
		*target = small;
}

static int detectLogo(LogoTarget* target) {
	Buf buf;
	bufInit(&buf);
	char id[64] = "";
	char idLike[128] = "";
	if (readFileInto("/etc/os-release", &buf) == 0) {
		parseOsRelease(buf.data, "ID", id, sizeof(id));
		parseOsRelease(buf.data, "ID_LIKE", idLike, sizeof(idLike));
	}
	bufFree(&buf);

	if (id[0] && logoLookup(id, target))
		return 1;
	char* save = NULL;
	for (char* token = strtok_r(idLike, " ", &save); token; token = strtok_r(NULL, " ", &save)) {
		if (logoLookup(token, target))
			return 1;
	}
	if (logoLookup("unknown", target))
		return 1;
	return logoLookup("linux", target);
}

static void stripVariant(char* name) {
	static const char* suffixes[] = { "_small", "-small" };
	size_t len = strlen(name);
	for (size_t i = 0; i < ARRAY_LEN(suffixes); i++) {
		size_t size = strlen(suffixes[i]);
		if (len > size && strcmp(name + len - size, suffixes[i]) == 0) {
			name[len - size] = '\0';
			return;
		}
	}
}

static int envInt(const char* name, int fallback) {
	const char* text = getenv(name);
	if (!text || !text[0])
		return fallback;
	return atoi(text);
}

int main(int argc, char** argv) {
	const char* logoName = NULL;
	int small = 0;
	int noColor = 0;
	int showIcons = 1;
	int live = 0;
	int imageMode = 1;
	int delayMs = 100;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (strcmp(arg, "--logo") == 0 || strcmp(arg, "-L") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "neomon: --logo needs a name\n");
				return 1;
			}
			logoName = argv[++i];
		} else if (strcmp(arg, "--big") == 0) {
			small = 0;
		} else if (strcmp(arg, "--small") == 0) {
			small = 1;
		} else if (strcmp(arg, "--no-color") == 0) {
			noColor = 1;
		} else if (strcmp(arg, "--no-icons") == 0) {
			showIcons = 0;
		} else if (strcmp(arg, "--live") == 0) {
			live = 1;
		} else if (strcmp(arg, "--image") == 0) {
			imageMode = 2;
		} else if (strcmp(arg, "--no-image") == 0) {
			imageMode = 0;
		} else if (strcmp(arg, "--delay") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "neomon: --delay needs a value\n");
				return 1;
			}
			delayMs = atoi(argv[++i]);
		} else if (strcmp(arg, "--list") == 0) {
			Buf list;
			bufInit(&list);
			logoList(&list);
			fwrite(list.data, 1, list.len, stdout);
			bufFree(&list);
			return 0;
		} else if (strcmp(arg, "--version") == 0) {
			puts("neomon " VERSION);
			return 0;
		} else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			printHelp();
			return 0;
		} else {
			fprintf(stderr, "neomon: unknown option %s\n", arg);
			printHelp();
			return 1;
		}
	}

	if (delayMs < 0)
		delayMs = 0;
	if (delayMs > 2000)
		delayMs = 2000;

	if (live && !termIsTty()) {
		fprintf(stderr, "neomon: --live needs a terminal\n");
		return 1;
	}

	LogoTarget target;
	memset(&target, 0, sizeof(target));
	if (logoName) {
		if (!logoLookup(logoName, &target)) {
			fprintf(stderr, "neomon: unknown logo '%s', try --list\n", logoName);
			return 1;
		}
	} else if (!detectLogo(&target)) {
		fprintf(stderr, "neomon: no logo found in %s\n", logoDir());
		return 1;
	}
	if (small)
		useSmall(&target);

	LogoArt art;
	if (logoLoad(&target, &art) != 0) {
		fprintf(stderr, "neomon: cannot read logo '%s' in %s\n", target.file, logoDir());
		return 1;
	}

	Theme theme;
	themeInit(&theme, target.colors, !noColor && termColorable());

	Info info;
	memset(&info, 0, sizeof(info));
	if (!live)
		getInfo(&info, delayMs);

	Image image;
	Image* imagePtr = NULL;
	int imageKind = (imageMode == 0 || live) ? IMAGE_NONE : imageDetect();
	if (imageMode == 2 && imageKind == IMAGE_NONE)
		imageKind = IMAGE_KITTY;
	if (imageKind != IMAGE_NONE && termIsTty()) {
		char base[128];
		char path[512];
		snprintf(base, sizeof(base), "%s", target.file);
		stripVariant(base);
		if (imageFind(base, path, sizeof(path)) == 0) {
			int cols = envInt("NEOMON_IMAGE_COLS", 28);
			int rows = envInt("NEOMON_IMAGE_ROWS", 14);
			if (imageLoad(&image, path, imageKind, cols, rows) == 0)
				imagePtr = &image;
		}
	}

	Buf out;
	Buf screen;
	bufInit(&out);
	bufInit(&screen);

	if (live) {
		signal(SIGINT, stopLive);
		signal(SIGTERM, stopLive);
		termLiveBegin();
		while (gRunning) {
			getInfo(&info, delayMs);
			bufClear(&out);
			renderOutput(&out, &art, &theme, &info, termWidth(), showIcons, NULL);
			liveFrame(&out, &screen);
			fwrite(screen.data, 1, screen.len, stdout);
			fflush(stdout);
			sleepMs(LIVE_INTERVAL_MS);
		}
		termLiveEnd();
	} else {
		renderOutput(&out, &art, &theme, &info, termWidth(), showIcons, imagePtr);
		fwrite(out.data, 1, out.len, stdout);
	}

	bufFree(&screen);
	bufFree(&out);
	logoFree(&art);
	if (imagePtr)
		imageFree(imagePtr);
	return 0;
}

#include "logo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef NEOMON_LOGO_DIR
#define NEOMON_LOGO_DIR "/usr/local/share/neomon/ascii"
#endif

static void rstrip(char* text) {
	size_t len = strlen(text);
	while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\r' || text[len - 1] == '\t'))
		text[--len] = '\0';
}

static int lineWidth(const char* text) {
	int width = 0;
	for (const char* p = text; *p;) {
		if (p[0] == '$' && p[1] == '$') {
			width++;
			p += 2;
			continue;
		}
		if (p[0] == '$' && p[1] >= '1' && p[1] <= '9') {
			p += 2;
			continue;
		}
		if (((unsigned char)*p & 0xC0) != 0x80)
			width++;
		p++;
	}
	return width;
}

const char* logoDir(void) {
	const char* env = getenv("NEOMON_LOGO_DIR");
	if (env && env[0] && access(env, R_OK) == 0)
		return env;
	if (access(NEOMON_LOGO_DIR, R_OK) == 0)
		return NEOMON_LOGO_DIR;
	if (access("assets/ascii", R_OK) == 0)
		return "assets/ascii";
	return NEOMON_LOGO_DIR;
}

void logoCompactName(const char* file, char* out, size_t size) {
	static const char* suffixes[] = { "_small", "-small" };
	char base[128];
	size_t len;

	if (size == 0)
		return;
	out[0] = '\0';
	if (!file || !file[0])
		return;
	snprintf(base, sizeof(base), "%s", file);
	len = strlen(base);
	for (size_t i = 0; i < ARRAY_LEN(suffixes); i++) {
		size_t n = strlen(suffixes[i]);
		if (len > n && strcmp(base + len - n, suffixes[i]) == 0) {
			base[len - n] = '\0';
			len -= n;
			break;
		}
	}
	snprintf(out, size, "%s_small", base);
}

int logoLookup(const char* name, LogoTarget* out) {
	if (!name || !name[0])
		return 0;
	for (unsigned int i = 0; i < g_logoAliasCount; i++) {
		if (strcasecmp(g_logoAliases[i].name, name) != 0)
			continue;
		const char* file = g_logoAliases[i].file;
		out->file = file;
		out->isSmall = g_logoAliases[i].isSmall;
		out->isAlt = g_logoAliases[i].isAlt;
		for (int c = 0; c < LOGO_MAX_COLORS; c++)
			out->colors[c] = "";
		for (unsigned int j = 0; j < g_logoPaletteCount; j++) {
			if (strcmp(g_logoPalettes[j].file, file) != 0)
				continue;
			for (int c = 0; c < LOGO_MAX_COLORS; c++)
				out->colors[c] = g_logoPalettes[j].colors[c];
			break;
		}
		return 1;
	}
	return 0;
}

int logoLoad(const LogoTarget* target, LogoArt* art) {
	art->text = NULL;
	art->lines = NULL;
	art->count = 0;
	art->width = 0;
	snprintf(art->file, sizeof(art->file), "%s", target->file);
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.txt", logoDir(), target->file);
	size_t len = 0;
	char* text = readFileAlloc(path, &len);
	if (!text)
		return -1;
	art->text = text;
	int cap = 64;
	art->lines = malloc(sizeof(char*) * (size_t)cap);
	if (!art->lines) {
		free(text);
		art->text = NULL;
		return -1;
	}
	int count = 0;
	char* cursor = text;
	for (;;) {
		char* newline = strchr(cursor, '\n');
		if (newline)
			*newline = '\0';
		rstrip(cursor);
		if (count == cap) {
			cap *= 2;
			char** grown = realloc(art->lines, sizeof(char*) * (size_t)cap);
			if (!grown) {
				free(art->lines);
				free(text);
				art->lines = NULL;
				art->text = NULL;
				return -1;
			}
			art->lines = grown;
		}
		art->lines[count++] = cursor;
		if (!newline)
			break;
		cursor = newline + 1;
	}
	while (count > 0 && art->lines[count - 1][0] == '\0')
		count--;
	art->count = count;
	for (int i = 0; i < count; i++) {
		int width = lineWidth(art->lines[i]);
		if (width > art->width)
			art->width = width;
	}
	return 0;
}

void logoFree(LogoArt* art) {
	free(art->lines);
	free(art->text);
	art->lines = NULL;
	art->text = NULL;
	art->count = 0;
	art->width = 0;
}

void logoRenderLine(const LogoArt* art, int index, const Theme* theme, Buf* out, int* visibleWidth) {
	int width = 0;
	if (index < 0 || index >= art->count) {
		*visibleWidth = 0;
		return;
	}
	const char* p = art->lines[index];
	sgrAccent(out, theme, 0);
	while (*p) {
		if (p[0] == '$' && p[1] == '$') {
			bufAddChar(out, '$');
			width++;
			p += 2;
			continue;
		}
		if (p[0] == '$' && p[1] >= '1' && p[1] <= '9') {
			sgrAccent(out, theme, p[1] - '1');
			p += 2;
			continue;
		}
		bufAddChar(out, *p);
		if (((unsigned char)*p & 0xC0) != 0x80)
			width++;
		p++;
	}
	sgrReset(out, theme);
	*visibleWidth = width;
}

void logoList(Buf* out) {
	int written = 0;
	for (unsigned int i = 0; i < g_logoAliasCount; i++) {
		if (g_logoAliases[i].isSmall || g_logoAliases[i].isAlt)
			continue;
		bufAddF(out, "%-24s", g_logoAliases[i].name);
		if (++written % 3 == 0)
			bufAddChar(out, '\n');
	}
	if (written % 3 != 0)
		bufAddChar(out, '\n');
}

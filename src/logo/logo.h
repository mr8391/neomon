#pragma once

#include "../core/core.h"

typedef struct {
	char* text;
	char** lines;
	int count;
	int width;
	char file[128];
} LogoArt;

typedef struct {
	const char* file;
	const char* colors[LOGO_MAX_COLORS];
	int isSmall;
	int isAlt;
} LogoTarget;

const char* logoDir(void);
void logoCompactName(const char* file, char* out, size_t size);
int logoLookup(const char* name, LogoTarget* out);
int logoLoad(const LogoTarget* target, LogoArt* art);
void logoFree(LogoArt* art);
void logoRenderLine(const LogoArt* art, int index, const Theme* theme, Buf* out, int* visibleWidth);
void logoList(Buf* out);

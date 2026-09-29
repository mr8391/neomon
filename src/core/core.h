#pragma once

#include <stddef.h>

#include "../logo/palette.gen.h"

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
	char* data;
	size_t len;
	size_t cap;
} Buf;

void bufInit(Buf* buf);
void bufFree(Buf* buf);
void bufClear(Buf* buf);
void bufAddN(Buf* buf, const char* text, size_t len);
void bufAdd(Buf* buf, const char* text);
void bufAddChar(Buf* buf, char c);
void bufAddF(Buf* buf, const char* format, ...);

int readFileInto(const char* path, Buf* out);
char* readFileAlloc(const char* path, size_t* outLen);
int readTrimLine(const char* path, char* out, size_t size);
void strTrim(char* text);
char* strSkipSpace(char* text);

int termWidth(void);
int termColorable(void);
int termIsTty(void);
int utf8Width(const char* text, size_t len);
void b64Encode(const unsigned char* data, size_t len, Buf* out);
void cursorRight(Buf* buf, int columns);
void cursorUp(Buf* buf, int rows);
void cursorDown(Buf* buf, int rows);
void cursorCol(Buf* buf, int column);
void sleepMs(int ms);
void termLiveBegin(void);
void termLiveEnd(void);
void liveFrame(const Buf* frame, Buf* screen);

typedef struct {
	int enabled;
	const char* accent[LOGO_MAX_COLORS];
} Theme;

void themeInit(Theme* theme, const char* const* colors, int enabled);
void sgr(Buf* buf, const Theme* theme, const char* body);
void sgrReset(Buf* buf, const Theme* theme);
void sgrLabel(Buf* buf, const Theme* theme);
void sgrValue(Buf* buf, const Theme* theme);
void sgrAccent(Buf* buf, const Theme* theme, int index);

void fmtBytes(unsigned long long bytes, char* out, size_t size);
void fmtPair(unsigned long long used, unsigned long long total, char* out, size_t size);
void fmtRate(unsigned long long bytes, double seconds, char* out, size_t size);
void fmtUptime(double seconds, char* out, size_t size);
void fmtPct(char* out, size_t size, int pct);

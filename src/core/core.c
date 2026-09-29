#include "core.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

static void dieOom(void) {
	fputs("neomon: out of memory\n", stderr);
	exit(1);
}

void bufInit(Buf* buf) {
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

void bufFree(Buf* buf) {
	free(buf->data);
	bufInit(buf);
}

void bufClear(Buf* buf) {
	buf->len = 0;
	if (buf->data)
		buf->data[0] = '\0';
}

static void bufReserve(Buf* buf, size_t extra) {
	size_t need = buf->len + extra + 1;
	if (need <= buf->cap)
		return;
	size_t cap = buf->cap ? buf->cap : 64;
	while (cap < need)
		cap *= 2;
	char* data = realloc(buf->data, cap);
	if (!data)
		dieOom();
	buf->data = data;
	buf->cap = cap;
}

void bufAddN(Buf* buf, const char* text, size_t len) {
	bufReserve(buf, len);
	memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
}

void bufAdd(Buf* buf, const char* text) {
	bufAddN(buf, text, strlen(text));
}

void bufAddChar(Buf* buf, char c) {
	bufAddN(buf, &c, 1);
}

void bufAddF(Buf* buf, const char* format, ...) {
	va_list args;
	va_start(args, format);
	va_list copy;
	va_copy(copy, args);
	int count = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (count < 0) {
		va_end(args);
		return;
	}
	bufReserve(buf, (size_t)count);
	vsnprintf(buf->data + buf->len, (size_t)count + 1, format, args);
	buf->len += (size_t)count;
	va_end(args);
}

int readFileInto(const char* path, Buf* out) {
	FILE* file = fopen(path, "rb");
	if (!file)
		return -1;
	char chunk[4096];
	size_t count;
	while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0)
		bufAddN(out, chunk, count);
	int failed = ferror(file);
	fclose(file);
	return failed ? -1 : 0;
}

char* readFileAlloc(const char* path, size_t* outLen) {
	Buf buf;
	bufInit(&buf);
	if (readFileInto(path, &buf) != 0) {
		bufFree(&buf);
		return NULL;
	}
	if (!buf.data)
		bufAddChar(&buf, '\0');
	if (outLen)
		*outLen = buf.len;
	return buf.data;
}

char* strSkipSpace(char* text) {
	while (*text && isspace((unsigned char)*text))
		text++;
	return text;
}

void strTrim(char* text) {
	char* start = strSkipSpace(text);
	size_t len = strlen(start);
	if (start != text)
		memmove(text, start, len + 1);
	while (len > 0 && isspace((unsigned char)text[len - 1]))
		text[--len] = '\0';
}

int readTrimLine(const char* path, char* out, size_t size) {
	size_t len = 0;
	char* text = readFileAlloc(path, &len);
	if (!text)
		return -1;
	size_t count = 0;
	while (count < len && text[count] != '\n' && count + 1 < size) {
		out[count] = text[count];
		count++;
	}
	out[count] = '\0';
	strTrim(out);
	free(text);
	return 0;
}

int termWidth(void) {
#ifdef _WIN32
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO info;
	if (handle != NULL && handle != INVALID_HANDLE_VALUE &&
	    GetConsoleScreenBufferInfo(handle, &info))
		return info.srWindow.Right - info.srWindow.Left + 1;
	return 0;
#else
	struct winsize window;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0)
		return window.ws_col;
	return 0;
#endif
}

int termIsTty(void) {
	return isatty(fileno(stdout)) ? 1 : 0;
}

int termColorable(void) {
	if (getenv("NO_COLOR"))
		return 0;
	const char* term = getenv("TERM");
	if (term && strcmp(term, "dumb") == 0)
		return 0;
	return termIsTty();
}

int utf8Width(const char* text, size_t len) {
	int width = 0;
	for (size_t i = 0; i < len; i++) {
		if (((unsigned char)text[i] & 0xC0) != 0x80)
			width++;
	}
	return width;
}

void b64Encode(const unsigned char* data, size_t len, Buf* out) {
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (size_t i = 0; i < len; i += 3) {
		int left = (int)(len - i);
		unsigned int value = (unsigned int)data[i] << 16;
		if (left > 1)
			value |= (unsigned int)data[i + 1] << 8;
		if (left > 2)
			value |= (unsigned int)data[i + 2];
		bufAddChar(out, table[(value >> 18) & 63]);
		bufAddChar(out, table[(value >> 12) & 63]);
		bufAddChar(out, left > 1 ? table[(value >> 6) & 63] : '=');
		bufAddChar(out, left > 2 ? table[value & 63] : '=');
	}
}

void cursorRight(Buf* buf, int columns) {
	if (columns <= 0)
		return;
	bufAddF(buf, "\033[%dC", columns);
}

void cursorUp(Buf* buf, int rows) {
	if (rows <= 0)
		return;
	bufAddF(buf, "\033[%dA", rows);
}

void cursorDown(Buf* buf, int rows) {
	if (rows <= 0)
		return;
	bufAddF(buf, "\033[%dB", rows);
}

void cursorCol(Buf* buf, int column) {
	bufAddF(buf, "\033[%dG", column + 1);
}

void sleepMs(int ms) {
	if (ms <= 0)
		return;
#ifdef _WIN32
	Sleep((DWORD)ms);
#else
	usleep((useconds_t)ms * 1000);
#endif
}

void termLiveBegin(void) {
	fputs("\033[?1049h\033[?25l", stdout);
	fflush(stdout);
}

void termLiveEnd(void) {
	fputs("\033[?25h\033[?1049l", stdout);
	fflush(stdout);
}

#define LIVE_PAD_TOP 1
#define LIVE_PAD_LEFT 2

void liveFrame(const Buf* frame, Buf* screen) {
	size_t i = 0;
	bufClear(screen);
	bufAdd(screen, "\033[H\033[2J");
	for (int n = 0; n < LIVE_PAD_TOP; n++)
		bufAddChar(screen, '\n');
	while (i < frame->len) {
		const char* nl = memchr(frame->data + i, '\n', frame->len - i);
		size_t count = nl ? (size_t)(nl - (frame->data + i)) : frame->len - i;
		for (int n = 0; n < LIVE_PAD_LEFT; n++)
			bufAddChar(screen, ' ');
		bufAddN(screen, frame->data + i, count);
		bufAdd(screen, "\033[K");
		if (!nl)
			break;
		bufAddChar(screen, '\n');
		i += count + 1;
	}
}

static const char* bright(const char* body) {
	static const char* dull[] = { "30", "31", "32", "33", "34", "35", "36", "37" };
	static const char* vivid[] = { "90", "91", "92", "93", "94", "95", "96", "97" };
	if (!body || !body[0])
		return "";
	for (size_t i = 0; i < ARRAY_LEN(dull); i++) {
		if (strcmp(body, dull[i]) == 0)
			return vivid[i];
	}
	return body;
}

void themeInit(Theme* theme, const char* const* colors, int enabled) {
	theme->enabled = enabled;
	for (int i = 0; i < LOGO_MAX_COLORS; i++)
		theme->accent[i] = colors ? bright(colors[i]) : "";
}

void sgr(Buf* buf, const Theme* theme, const char* body) {
	if (!theme->enabled)
		return;
	bufAdd(buf, "\033[");
	bufAdd(buf, body);
	bufAddChar(buf, 'm');
}

void sgrReset(Buf* buf, const Theme* theme) {
	sgr(buf, theme, "0");
}

void sgrLabel(Buf* buf, const Theme* theme) {
	sgr(buf, theme, "2");
}

void sgrValue(Buf* buf, const Theme* theme) {
	sgr(buf, theme, "0");
}

void sgrAccent(Buf* buf, const Theme* theme, int index) {
	const char* body = "";
	if (index >= 0 && index < LOGO_MAX_COLORS)
		body = theme->accent[index];
	if (body[0] == '\0')
		body = "0";
	sgr(buf, theme, body);
}

void fmtBytes(unsigned long long bytes, char* out, size_t size) {
	static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double value = (double)bytes;
	int unit = 0;
	while (value >= 1024.0 && unit < 4) {
		value /= 1024.0;
		unit++;
	}
	if (unit == 0)
		snprintf(out, size, "%llu B", bytes);
	else if (unit == 1)
		snprintf(out, size, "%.0f KiB", value);
	else
		snprintf(out, size, "%.1f %s", value, units[unit]);
}

void fmtPair(unsigned long long used, unsigned long long total, char* out, size_t size) {
	static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double whole = (double)total;
	int unit = 0;
	while (whole >= 1024.0 && unit < 4) {
		whole /= 1024.0;
		unit++;
	}
	double part = (double)used;
	for (int i = 0; i < unit; i++)
		part /= 1024.0;
	if (unit == 0)
		snprintf(out, size, "%llu / %llu B", used, total);
	else if (unit == 1)
		snprintf(out, size, "%.0f / %.0f KiB", part, whole);
	else
		snprintf(out, size, "%.1f / %.1f %s", part, whole, units[unit]);
}

void fmtRate(unsigned long long bytes, double seconds, char* out, size_t size) {
	char rate[32];
	double value = seconds > 0 ? (double)bytes / seconds : 0;
	fmtBytes((unsigned long long)value, rate, sizeof(rate));
	snprintf(out, size, "%s/s", rate);
}

void fmtUptime(double seconds, char* out, size_t size) {
	long total = (long)seconds;
	long days = total / 86400;
	long hours = (total % 86400) / 3600;
	long minutes = (total % 3600) / 60;
	if (days > 0)
		snprintf(out, size, "%ldd %ldh", days, hours);
	else if (hours > 0)
		snprintf(out, size, "%ldh %ldm", hours, minutes);
	else
		snprintf(out, size, "%ldm", minutes);
}

void fmtPct(char* out, size_t size, int pct) {
	if (pct < 0)
		snprintf(out, size, "n/a");
	else
		snprintf(out, size, "%d%%", pct);
}

#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#ifndef R_OK
#define R_OK 4
#endif
#else
#include <unistd.h>
#endif

#ifndef NEOMON_ICON_DIR
#define NEOMON_ICON_DIR "/usr/local/share/neomon/img"
#endif

#define KITTY_CHUNK 4096

int imageDetect(void) {
	const char* program = getenv("TERM_PROGRAM");
	const char* terminal = getenv("LC_TERMINAL");
	const char* term = getenv("TERM");

	if (program && strcmp(program, "iTerm.app") == 0)
		return IMAGE_ITERM;
	if (terminal && strcmp(terminal, "iTerm2") == 0)
		return IMAGE_ITERM;
	if (getenv("KITTY_WINDOW_ID"))
		return IMAGE_KITTY;
	if (getenv("GHOSTTY_RESOURCES_DIR"))
		return IMAGE_KITTY;
	if (program && strcmp(program, "WezTerm") == 0)
		return IMAGE_KITTY;
	if (program && (strcmp(program, "ghostty") == 0 || strcmp(program, "Ghostty") == 0))
		return IMAGE_KITTY;
	if (term && strstr(term, "kitty"))
		return IMAGE_KITTY;
	return IMAGE_NONE;
}

static const char* imageDir(void) {
	const char* env = getenv("NEOMON_ICON_DIR");
	if (env && env[0] && access(env, R_OK) == 0)
		return env;
	if (access(NEOMON_ICON_DIR, R_OK) == 0)
		return NEOMON_ICON_DIR;
	if (access("assets/img", R_OK) == 0)
		return "assets/img";
	return NEOMON_ICON_DIR;
}

int imageFind(const char* base, char* out, size_t size) {
	if (!base || !base[0])
		return -1;
	snprintf(out, size, "%s/%s.png", imageDir(), base);
	return access(out, R_OK) == 0 ? 0 : -1;
}

static unsigned readBe32(const char* data) {
	const unsigned char* p = (const unsigned char*)data;
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static const unsigned char PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

int imageLoad(Image* image, const char* path, int kind, int cols, int rows) {
	memset(image, 0, sizeof(*image));
	if (kind == IMAGE_NONE)
		return -1;
	FILE* file = fopen(path, "rb");
	if (!file)
		return -1;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return -1;
	}
	long length = ftell(file);
	if (length < 24 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return -1;
	}
	char* data = malloc((size_t)length);
	if (!data) {
		fclose(file);
		return -1;
	}
	if (fread(data, 1, (size_t)length, file) != (size_t)length) {
		free(data);
		fclose(file);
		return -1;
	}
	fclose(file);
	if (memcmp(data, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0) {
		free(data);
		return -1;
	}
	image->kind = kind;
	image->width = readBe32(data + 16);
	image->height = readBe32(data + 20);
	image->cols = cols;
	image->rows = rows;
	image->data = data;
	image->size = (size_t)length;
	return image->width && image->height ? 0 : -1;
}

void imageFree(Image* image) {
	free(image->data);
	image->data = NULL;
	image->size = 0;
}

static void drawKitty(const Image* image, Buf* out, const char* encoded, size_t length) {
	size_t pos = 0;
	while (pos < length) {
		size_t take = length - pos;
		if (take > KITTY_CHUNK)
			take = KITTY_CHUNK;
		int more = pos + take < length;
		int mark = pos == 0 ? (more ? 1 : 0) : (more ? 2 : 0);
		bufAddF(out, "\033_Ga=T,f=100,s=%u,v=%u,c=%d,r=%d,m=%d;", image->width, image->height, image->cols, image->rows, mark);
		bufAddN(out, encoded + pos, take);
		bufAdd(out, "\033\\");
		pos += take;
	}
}

static void drawIterm(const Image* image, Buf* out, const char* encoded, size_t length) {
	bufAddF(out, "\033]1337;File=inline=1;width=%dch;height=%dch;preserveAspectRatio=1;size=%zu:",
		image->cols, image->rows, image->size);
	bufAddN(out, encoded, length);
	bufAdd(out, "\a");
}

void imageDraw(const Image* image, Buf* out) {
	Buf encoded;
	bufInit(&encoded);
	b64Encode((const unsigned char*)image->data, image->size, &encoded);
	bufAdd(out, "\033" "7");
	if (image->kind == IMAGE_KITTY)
		drawKitty(image, out, encoded.data, encoded.len);
	else
		drawIterm(image, out, encoded.data, encoded.len);
	bufAdd(out, "\033" "8");
	bufFree(&encoded);
}

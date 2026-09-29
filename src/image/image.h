#pragma once

#include "../core/core.h"

#define IMAGE_NONE 0
#define IMAGE_KITTY 1
#define IMAGE_ITERM 2

typedef struct {
	int kind;
	int cols;
	int rows;
	unsigned width;
	unsigned height;
	char* data;
	size_t size;
} Image;

int imageDetect(void);
int imageFind(const char* base, char* out, size_t size);
int imageLoad(Image* image, const char* path, int kind, int cols, int rows);
void imageFree(Image* image);
void imageDraw(const Image* image, Buf* out);

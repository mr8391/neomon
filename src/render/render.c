#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_W 11
#define BAR_W 8
#define GAP 3
#define RULE_MAX 54
#define SIDE_MARGIN 16

#define ICON_OS "\uf108"
#define ICON_KERNEL "\uf085"
#define ICON_UPTIME "\uf43a"
#define ICON_SHELL "\uf489"
#define ICON_TERM "\uea85"
#define ICON_PACKAGES "\uf487"
#define ICON_CPU "\uf2db"
#define ICON_GPU "\U000F08AE"
#define ICON_MEMORY "\uefc5"
#define ICON_VRAM "\uefc5"
#define ICON_SWAP "\uf0ec"
#define ICON_DISK "\uf0a0"
#define ICON_NETWORK "\uf1eb"
#define ICON_LOAD "\uf0e4"
#define ICON_BATTERY "\uf240"

typedef enum {
	ROW_TITLE,
	ROW_RULE,
	ROW_PLAIN,
	ROW_METRIC
} RowKind;

typedef struct {
	RowKind kind;
	const char* icon;
	const char* label;
	const char* value;
	const char* suffix;
	const char* sub;
	int pct;
	int hasBar;
} Row;

typedef struct {
	const Row* row;
	int line;
} Part;

static int visLen(const char* text) {
	return utf8Width(text, strlen(text));
}

static int padTo(Buf* out, int width, int target) {
	while (width < target) {
		bufAddChar(out, ' ');
		width++;
	}
	return width;
}

static const char* levelCode(int pct) {
	if (pct >= 85)
		return "31";
	if (pct >= 65)
		return "33";
	return NULL;
}

static void addBar(Buf* out, const Theme* theme, int pct, int width, const char* code) {
	static const char* partials[] = {
		"", "\u258F", "\u258E", "\u258D", "\u258C", "\u258B", "\u258A", "\u2589"
	};
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	double exact = (double)pct * width / 100.0;
	int full = (int)exact;
	int eighths = (int)((exact - full) * 8.0);
	if (eighths > 7)
		eighths = 7;
	if (code)
		sgr(out, theme, code);
	else
		sgrValue(out, theme);
	for (int i = 0; i < full; i++)
		bufAdd(out, "\u2588");
	if (eighths > 0)
		bufAdd(out, partials[eighths]);
	sgrLabel(out, theme);
	for (int i = full + (eighths > 0 ? 1 : 0); i < width; i++)
		bufAdd(out, "\u2591");
	sgrValue(out, theme);
}

static int addLabel(Buf* out, const Row* row, const Theme* theme, int icons, int width) {
	if (icons) {
		char key[40];
		snprintf(key, sizeof(key), "NEOMON_ICON_%s", row->label);
		const char* override = getenv(key);
		const char* glyph = (override && override[0]) ? override : row->icon;
		sgrValue(out, theme);
		bufAdd(out, glyph);
		width += visLen(glyph);
		bufAddChar(out, ' ');
		width += 1;
	}
	sgrLabel(out, theme);
	bufAdd(out, row->label);
	width += visLen(row->label);
	return padTo(out, width, TEXT_W);
}

static void addDim(Buf* out, const Theme* theme, const char* text, int* width) {
	sgrLabel(out, theme);
	bufAdd(out, "  ");
	bufAdd(out, text);
	*width += 2 + visLen(text);
}

static void addInlinePct(Buf* out, const Theme* theme, int pct, int* width) {
	char text[16];
	const char* code = levelCode(pct);
	snprintf(text, sizeof(text), "  %d%%", pct);
	if (code)
		sgr(out, theme, code);
	else
		sgrLabel(out, theme);
	bufAdd(out, text);
	*width += visLen(text);
	sgrValue(out, theme);
}

static int renderRow(Buf* out, const Row* row, const Theme* theme, int icons, int ruleLen) {
	int width = 0;
	switch (row->kind) {
	case ROW_TITLE:
		sgrValue(out, theme);
		bufAdd(out, row->label);
		width += visLen(row->label);
		sgrLabel(out, theme);
		bufAddChar(out, '@');
		width += 1;
		sgr(out, theme, "1");
		bufAdd(out, row->value);
		width += visLen(row->value);
		sgrReset(out, theme);
		break;
	case ROW_RULE:
		sgrLabel(out, theme);
		for (int i = 0; i < ruleLen; i++) {
			bufAdd(out, "\u2500");
			width++;
		}
		sgrReset(out, theme);
		break;
	case ROW_PLAIN:
		width = addLabel(out, row, theme, icons, width);
		sgrValue(out, theme);
		bufAdd(out, row->value);
		width += visLen(row->value);
		sgrReset(out, theme);
		break;
	case ROW_METRIC:
		width = addLabel(out, row, theme, icons, width);
		sgrValue(out, theme);
		bufAdd(out, row->value);
		width += visLen(row->value);
		if (!row->hasBar && !row->sub) {
			if (row->pct >= 0)
				addInlinePct(out, theme, row->pct, &width);
			if (row->suffix && row->suffix[0])
				addDim(out, theme, row->suffix, &width);
		}
		sgrReset(out, theme);
		break;
	}
	return width;
}

static int renderBarLine(Buf* out, const Row* row, const Theme* theme) {
	int width = padTo(out, 0, TEXT_W);
	const char* code = levelCode(row->pct);
	addBar(out, theme, row->pct, BAR_W, code);
	width += BAR_W;
	if (row->pct >= 0) {
		if (code)
			sgr(out, theme, code);
		else
			sgrValue(out, theme);
		bufAddF(out, " %3d%%", row->pct);
		width += 5;
	}
	if (row->suffix && row->suffix[0])
		addDim(out, theme, row->suffix, &width);
	sgrReset(out, theme);
	return width;
}

static int renderSubLine(Buf* out, const Row* row, const Theme* theme) {
	int width = padTo(out, 0, TEXT_W);
	sgrLabel(out, theme);
	bufAdd(out, row->sub);
	width += visLen(row->sub);
	sgrReset(out, theme);
	return width;
}

static int renderPart(Buf* out, const Part* part, const Theme* theme, int icons) {
	if (part->line == 1)
		return renderBarLine(out, part->row, theme);
	if (part->line == 2)
		return renderSubLine(out, part->row, theme);
	return renderRow(out, part->row, theme, icons, 0);
}

static int partWidth(const Part* part) {
	const Row* row = part->row;
	if (part->line == 1) {
		int width = TEXT_W + BAR_W + 5;
		if (row->suffix && row->suffix[0])
			width += 2 + visLen(row->suffix);
		return width;
	}
	if (part->line == 2)
		return TEXT_W + visLen(row->sub);
	int width = TEXT_W + visLen(row->value);
	if (row->kind == ROW_METRIC && !row->hasBar && !row->sub) {
		if (row->pct >= 0) {
			char text[16];
			snprintf(text, sizeof(text), "  %d%%", row->pct);
			width += visLen(text);
		}
		if (row->suffix && row->suffix[0])
			width += 2 + visLen(row->suffix);
	}
	return width;
}

static int columnWidth(Part* parts, int count) {
	int width = 0;
	for (int i = 0; i < count; i++) {
		int part = partWidth(&parts[i]);
		if (part > width)
			width = part;
	}
	return width;
}

static int expand(Row* rows, int count, Part* parts, int capacity) {
	int total = 0;
	for (int i = 0; i < count && total + 2 <= capacity; i++) {
		parts[total].row = &rows[i];
		parts[total].line = 0;
		total++;
		if (rows[i].hasBar) {
			parts[total].row = &rows[i];
			parts[total].line = 1;
			total++;
		} else if (rows[i].sub && rows[i].sub[0]) {
			parts[total].row = &rows[i];
			parts[total].line = 2;
			total++;
		}
	}
	return total;
}

typedef struct {
	const Row* header;
	Part* left;
	int leftCount;
	int leftWidth;
	Part* right;
	int rightCount;
	int infoLines;
	int side;
	int ruleLen;
} InfoBlock;

static int renderInfoLine(Buf* out, const InfoBlock* block, int index, const Theme* theme, int icons) {
	int width = 0;

	if (index < 0 || index >= block->infoLines)
		return 0;
	if (index == 0 || index == 1)
		return renderRow(out, &block->header[index], theme, icons, block->ruleLen);
	if (block->side) {
		int j = index - 2;
		if (j >= 0 && j < block->leftCount)
			width = renderPart(out, &block->left[j], theme, icons);
		width = padTo(out, width, block->leftWidth);
		for (int g = 0; g < GAP; g++)
			bufAddChar(out, ' ');
		if (j >= 0 && j < block->rightCount)
			renderPart(out, &block->right[j], theme, icons);
		return width;
	}
	if (index < 2 + block->leftCount)
		return renderPart(out, &block->left[index - 2], theme, icons);
	return renderPart(out, &block->right[index - 2 - block->leftCount], theme, icons);
}

void renderOutput(Buf* out, const LogoArt* art, const Theme* theme, const Info* info, int maxWidth, int showIcons, const Image* image) {
	char cpuValue[256];
	char netLine[128];
	char netSub[96];
	char diskValues[MAX_DISKS][96];
	Row header[2];
	Row leftRows[20];
	Row rightRows[40];
	Part leftParts[60];
	Part rightParts[120];
	InfoBlock block;
	int leftCount = 0;
	int rightCount = 0;
	int leftN;
	int rightN;
	int leftW;
	int rightW;
	int infoWide;
	int stackW;
	int available = maxWidth > 0 ? maxWidth : 1000000;
	int picture = 0;
	int side = 0;
	int contentW = 0;
	int pictureRows = 0;
	int pictureCols = 0;
	int infoCol = 0;
	int infoH;
	int pictureH;
	int span;
	int pictureTop;
	int infoTop;
	int ruleLen;
	Image sized;
	const LogoArt* artOut = art;
	LogoArt compact;
	int compactLoaded = 0;

	if (info->cpuSpec[0])
		snprintf(cpuValue, sizeof(cpuValue), "%s  %s", info->cpuModel, info->cpuSpec);
	else
		snprintf(cpuValue, sizeof(cpuValue), "%s", info->cpuModel);
	snprintf(netLine, sizeof(netLine), "%s  %s", info->netName, info->netIp);
	snprintf(netSub, sizeof(netSub), "\u2193 %s   \u2191 %s", info->netDown, info->netUp);
	for (int i = 0; i < info->diskCount; i++)
		snprintf(diskValues[i], sizeof(diskValues[i]), "%s", info->disks[i].pair);

	header[0] = (Row){ .kind = ROW_TITLE, .label = info->user, .value = info->host };
	header[1] = (Row){ .kind = ROW_RULE };

	leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_OS, .label = "OS", .value = info->os };
	leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_KERNEL, .label = "Kernel", .value = info->kernel };
	leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_UPTIME, .label = "Uptime", .value = info->uptime };
	leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_SHELL, .label = "Shell", .value = info->shell };
	leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_TERM, .label = "Terminal", .value = info->terminal };
	if (info->packages[0])
		leftRows[leftCount++] = (Row){ .kind = ROW_PLAIN, .icon = ICON_PACKAGES, .label = "Packages", .value = info->packages };
	leftRows[leftCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_CPU, .label = "CPU", .value = cpuValue, .pct = info->cpuPct, .hasBar = 1, .suffix = info->cpuTemp };
	if (info->hasGpu)
		leftRows[leftCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_GPU, .label = "GPU", .value = info->gpuModel, .pct = info->gpuPct, .hasBar = info->gpuPct >= 0, .suffix = info->gpuTemp };

	if (info->hasVram)
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_VRAM, .label = "VRAM", .value = info->gpuMemText, .pct = info->gpuMemPct };
	if (info->memText[0])
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_MEMORY, .label = "Memory", .value = info->memText, .pct = info->memPct };
	if (info->hasSwap)
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_SWAP, .label = "Swap", .value = info->swapText, .pct = info->swapPct };
	for (int i = 0; i < info->diskCount; i++) {
		snprintf(diskValues[i], sizeof(diskValues[i]), "%s", info->disks[i].pair);
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_DISK, .label = info->disks[i].mount, .value = diskValues[i], .pct = info->disks[i].pct };
	}
	if (info->hasNet)
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_NETWORK, .label = "Network", .value = netLine, .sub = netSub };
	if (info->load[0])
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_LOAD, .label = "Load", .value = info->load, .sub = info->procs };
	if (info->hasBattery)
		rightRows[rightCount++] = (Row){ .kind = ROW_METRIC, .icon = ICON_BATTERY, .label = "Battery", .value = info->battery };

	leftN = expand(leftRows, leftCount, leftParts, (int)ARRAY_LEN(leftParts));
	rightN = expand(rightRows, rightCount, rightParts, (int)ARRAY_LEN(rightParts));
	leftW = columnWidth(leftParts, leftN);
	rightW = columnWidth(rightParts, rightN);
	infoWide = leftW + GAP + rightW;
	stackW = leftW > rightW ? leftW : rightW;

	int contents[2] = { infoWide, stackW };
	int sides[2] = { 1, 0 };

	if (image && image->kind != IMAGE_NONE) {
		for (int i = 0; i < 2 && picture == 0; i++) {
			int freeWidth = available - GAP - contents[i] - 1;
			int rows = freeWidth / 2;
			if (image->rows > 0 && rows > image->rows)
				rows = image->rows;
			if (rows >= 6) {
				picture = 2;
				side = sides[i];
				contentW = contents[i];
				pictureRows = rows;
				pictureCols = rows * 2;
				infoCol = pictureCols + GAP;
			}
		}
	}
	if (picture == 0 && art->count > 0) {
		for (int i = 0; i < 2 && picture == 0; i++) {
			if (available <= 0 || art->width + GAP + contents[i] <= available) {
				picture = 1;
				side = sides[i];
				contentW = contents[i];
			}
		}
	}
	if (picture == 0 && art->count > 0 && art->file[0]) {
		char name[160];
		LogoTarget target;
		logoCompactName(art->file, name, sizeof(name));
		if (logoLookup(name, &target) && logoLoad(&target, &compact) == 0) {
			for (int i = 0; i < 2 && picture == 0; i++) {
				if (available <= 0 || compact.width + GAP + contents[i] <= available) {
					picture = 1;
					side = sides[i];
					contentW = contents[i];
					artOut = &compact;
					compactLoaded = 1;
				}
			}
			if (!compactLoaded)
				logoFree(&compact);
		}
	}
	if (picture == 0) {
		side = available <= 0 || infoWide <= available;
		contentW = side ? infoWide : stackW;
	}

	infoH = 2 + (side ? (leftN > rightN ? leftN : rightN) : leftN + rightN);
	pictureH = picture == 2 ? pictureRows : (picture == 1 ? artOut->count : 0);
	span = pictureH > infoH ? pictureH : infoH;
	pictureTop = (span - pictureH) / 2;
	infoTop = (span - infoH) / 2;
	ruleLen = contentW < RULE_MAX ? contentW : RULE_MAX;
	if (ruleLen < 20)
		ruleLen = 20;

	block.header = header;
	block.left = leftParts;
	block.leftCount = leftN;
	block.leftWidth = leftW;
	block.right = rightParts;
	block.rightCount = rightN;
	block.infoLines = infoH;
	block.side = side;
	block.ruleLen = ruleLen;

	Buf logo;
	bufInit(&logo);

	if (picture == 2) {
		for (int n = 0; n < span; n++)
			bufAddChar(out, '\n');
		cursorUp(out, span - 1 - pictureTop);
		sized = *image;
		sized.cols = pictureCols;
		sized.rows = pictureRows;
		imageDraw(&sized, out);
		int delta = infoTop - pictureTop;
		if (delta > 0)
			cursorDown(out, delta);
		else if (delta < 0)
			cursorUp(out, -delta);
		cursorRight(out, infoCol);

		for (int j = 0; j < infoH; j++) {
			size_t lineStart = out->len;
			if (j > 0) {
				bufAddChar(out, '\n');
				cursorCol(out, infoCol);
			}
			renderInfoLine(out, &block, j, theme, showIcons);
			while (out->len > lineStart && out->data[out->len - 1] == ' ')
				out->len--;
			if (out->data)
				out->data[out->len] = '\0';
		}
		int extra = span - infoTop - infoH;
		if (extra > 0)
			cursorDown(out, extra);
		bufAdd(out, "\r\n");
	} else {
		for (int i = 0; i < span; i++) {
			size_t lineStart = out->len;
			if (picture == 1 && artOut->count > 0) {
				int picIndex = i - pictureTop;
				int logoWidth = 0;
				bufClear(&logo);
				if (picIndex >= 0 && picIndex < artOut->count)
					logoRenderLine(artOut, picIndex, theme, &logo, &logoWidth);
				bufAddN(out, logo.data ? logo.data : "", logo.len);
				padTo(out, logoWidth, artOut->width);
				for (int g = 0; g < GAP; g++)
					bufAddChar(out, ' ');
			}
			renderInfoLine(out, &block, i - infoTop, theme, showIcons);
			while (out->len > lineStart && out->data[out->len - 1] == ' ')
				out->len--;
			if (out->data)
				out->data[out->len] = '\0';
			bufAddChar(out, '\n');
		}
	}

	bufFree(&logo);
	if (compactLoaded)
		logoFree(&compact);
}

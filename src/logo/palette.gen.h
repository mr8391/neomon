#pragma once

#define LOGO_MAX_COLORS 9

typedef struct {
	const char* file;
	const char* colors[LOGO_MAX_COLORS];
} LogoPalette;

typedef struct {
	const char* name;
	const char* file;
	unsigned char isSmall;
	unsigned char isAlt;
} LogoAlias;

extern const LogoPalette g_logoPalettes[];
extern const unsigned int g_logoPaletteCount;
extern const LogoAlias g_logoAliases[];
extern const unsigned int g_logoAliasCount;

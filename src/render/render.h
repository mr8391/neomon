#pragma once

#include "../core/core.h"
#include "../image/image.h"
#include "../logo/logo.h"
#include "../sys/sys.h"

void renderOutput(Buf* out, const LogoArt* art, const Theme* theme, const Info* info, int maxWidth, int showIcons, const Image* image);

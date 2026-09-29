# neomon

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Language: C11](https://img.shields.io/badge/language-C11-555.svg)](#build)

A command line system information tool with usage bars and real time monitoring

> [!NOTE]
> A plain run takes one sample over `--delay` milliseconds. Pass `--live`
> when you want it to keep sampling until Ctrl+C.

## Build

Requires a C11 compiler and `make`. No runtime dependencies.

```
make
sudo make install
```

The Makefile selects `src/sys/linux.c`, `apple.c` or `win32.c` from `uname`.
Override with `make PLATFORM=...`. `make install` copies the binary to
`$(PREFIX)/bin/neomon`, the ASCII art to `$(PREFIX)/share/neomon/ascii`
and the brand marks to `$(PREFIX)/share/neomon/img`.

## Usage

```
neomon [options]

  -L, --logo NAME   force a distro logo by name
      --big         use the full size logo (default)
      --small       use the compact logo
      --live        refresh usage in real time until Ctrl+C
      --image       use a PNG logo when the terminal renders images
      --no-image    never use a PNG logo
      --delay MS    sampling window for cpu and net, default 100
      --no-color    disable colors
      --no-icons    disable label icons
      --list        list known logo names
  -h, --help        show help
      --version     show the version
```

### Live mode

`--live` redraws the whole block in the alternate screen and keeps sampling
until Ctrl+C. Each frame still measures over `--delay`, and the picture stays
ASCII art because the screen is redrawn rather than appended to.

### Picture selection

The picture is chosen by terminal width: the full art with two info columns
when both fit, the full art with one column when two do not, the compact art
when the full art will not fit, and no picture below that. Logo and info
block are centered against each other.

### Paths and overrides

| What | Order |
|---|---|
| ASCII art | `$NEOMON_LOGO_DIR`, `$(PREFIX)/share/neomon/ascii`, `./assets/ascii` |
| Brand marks | `$NEOMON_ICON_DIR`, `$(PREFIX)/share/neomon/img`, `./assets/img` |

- `NEOMON_ICON_<LABEL>` replaces one row icon, for example `NEOMON_ICON_GPU`.
- `NEOMON_IMAGE_COLS` and `NEOMON_IMAGE_ROWS` cap the image logo size.
- `NO_COLOR=1` disables colors, same as `--no-color`.

> [!TIP]
> Row icons are raw Nerd Font glyphs with no ASCII fallback. Use `--no-icons`
> when the font in your terminal cannot display them.

## Image logos

Terminals that speak the Kitty graphics protocol (kitty, Ghostty, WezTerm) or
iTerm2 inline images print the real brand mark instead of ASCII art. Detection
is automatic, so the common case needs no flag. `--image` forces it and
`--no-image` disables it. Output to a file or pipe never contains image data.

## Attributions

- ASCII art in `assets/ascii/`, with palettes from
  [fastfetch](https://github.com/fastfetch-cli/fastfetch) (MIT), which carries
  art derived from [neofetch](https://github.com/dylanaraps/neofetch) (MIT)
  and community contributions.
- Brand marks in `assets/img/`, from
  [simple-icons](https://simpleicons.org), licensed CC0-1.0, MIT, CC-BY-3.0
  or CC-BY-4.0. All 120 bundled marks are listed in `assets/img/LICENSES.md`.
- Row icons use Nerd Font codepoints (MIT). No font is bundled.

## License

MIT. See [LICENSE](LICENSE).

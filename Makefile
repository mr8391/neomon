CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local
LOGO_DIR ?= $(PREFIX)/share/neomon/ascii
ICON_DIR ?= $(PREFIX)/share/neomon/img
CPPFLAGS += -DNEOMON_LOGO_DIR=\"$(LOGO_DIR)\" -DNEOMON_ICON_DIR=\"$(ICON_DIR)\"

UNAME := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME),Linux)
PLATFORM ?= linux
else ifeq ($(UNAME),Darwin)
PLATFORM ?= apple
else
PLATFORM ?= win32
endif

ifeq ($(PLATFORM),win32)
LDFLAGS += -ladvapi32
endif

SRC := src/main.c \
	src/core/core.c \
	src/image/image.c \
	src/logo/logo.c \
	src/logo/palette.gen.c \
	src/render/render.c \
	src/sys/sys.c \
	src/sys/$(PLATFORM).c
OBJ := $(SRC:.c=.o)
BIN := neomon

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -c -o $@ $<

tests/test: $(filter-out src/main.o,$(OBJ)) tests/test.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: tests/test
	./tests/test

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	mkdir -p $(DESTDIR)$(LOGO_DIR) $(DESTDIR)$(ICON_DIR)
	cp -f assets/ascii/*.txt $(DESTDIR)$(LOGO_DIR)/
	cp -f assets/img/*.png $(DESTDIR)$(ICON_DIR)/
	cp -f assets/img/LICENSES.md $(DESTDIR)$(ICON_DIR)/

clean:
	rm -f $(OBJ) tests/test.o tests/test $(BIN)

.PHONY: all test install clean

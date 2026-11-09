CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra
TARGET = ea-music
PREFIX ?= /usr
DESTDIR ?=

SOURCES = src/main.c \
          src/ui.c \
          src/components/search_box.c \
          src/components/library_dialog.c \
          src/components/library_manager.c \
          src/backend/ytdlp.c

RAYLIB_CFLAGS = $(shell pkg-config --cflags raylib)
RAYLIB_LIBS = $(shell pkg-config --libs raylib)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) $(SOURCES) -o $@ $(RAYLIB_LIBS) -lm -lpthread

install: $(TARGET)
	install -Dm755 $(TARGET) "$(DESTDIR)$(PREFIX)/bin/$(TARGET)"

clean:
	rm -f $(TARGET)


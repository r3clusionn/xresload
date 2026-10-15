CFLAGS ?= -O2 -march=native -pipe
CFLAGS += -std=c99 -Wall -Wextra
PREFIX ?= $(HOME)/.local

xresload: xresload.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lxcb

install: xresload
	install -Dm755 xresload $(PREFIX)/bin/xresload

clean:
	rm -f xresload

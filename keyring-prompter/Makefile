CC ?= gcc
PKGS = gcr-base-3 gio-unix-2.0 json-glib-1.0
CFLAGS ?= -O2 -g
all: tacit-keyring-prompter
tacit-keyring-prompter: tacit-keyring-prompter.c
	$(CC) $(CFLAGS) -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS)) -o $@ $< $(shell pkg-config --libs $(PKGS))
clean:
	rm -f tacit-keyring-prompter

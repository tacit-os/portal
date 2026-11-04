HYPRVER := $(shell pkg-config --modversion hyprland 2>/dev/null)

all: tacit-share.so

# Rebuild for every Hyprland version: the API version and the client hash are baked in at compile time and the
# compositor refuses a stale plugin, so the installed version is the prerequisite that has to be watched.
tacit-share.so: tacit-share.cpp .hyprver
	g++ -shared -fPIC -std=c++26 -O2 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags hyprland pixman-1 libdrm) -o $@ $<

.hyprver: FORCE
	@[ "$$(cat $@ 2>/dev/null)" = "$(HYPRVER)" ] || echo "$(HYPRVER)" > $@

clean:
	rm -f tacit-share.so .hyprver

.PHONY: all clean FORCE
FORCE:

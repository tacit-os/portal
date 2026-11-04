# from hyprland-protocols

share-watch: share-watch.c hyprland-toplevel-export-v1-protocol.c hyprland-toplevel-export-v1-client.h
	cc -O2 -Wall -o $@ share-watch.c hyprland-toplevel-export-v1-protocol.c $(shell pkg-config --cflags --libs wayland-client)

hyprland-toplevel-export-v1-client.h: hyprland-toplevel-export-v1.xml
	wayland-scanner client-header hyprland-toplevel-export-v1.xml $@
hyprland-toplevel-export-v1-protocol.c: hyprland-toplevel-export-v1.xml
	wayland-scanner private-code hyprland-toplevel-export-v1.xml $@

clean:
	rm -f share-watch hyprland-toplevel-export-v1-*.[ch]

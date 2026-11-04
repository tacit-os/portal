// share-watch: shares one window the way Hyprland's portal does (hyprland-toplevel-export, copy without damage,
// the next frame asked for a frame interval after the last one is ready) and prints, once a second, how many frames
// arrived and how many of them differed from the one before: a frozen share shows frames that never differ.
//   share-watch <window address, 0x...> <seconds> [fps]
#define _GNU_SOURCE
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "hyprland-toplevel-export-v1-client.h"

// only capture_toplevel (version 1) is used; version 2's request names this wlr interface
const struct wl_interface zwlr_foreign_toplevel_handle_v1_interface = {"zwlr_foreign_toplevel_handle_v1", 3, 0, NULL, 0, NULL};

static struct wl_shm*                                g_shm;
static struct hyprland_toplevel_export_manager_v1*   g_mgr;
static struct hyprland_toplevel_export_frame_v1*     g_frame;
static struct wl_buffer*                             g_buf;
static void*                                         g_data;
static uint32_t g_fmt, g_w, g_h, g_stride, g_handle;
static size_t   g_size;
static uint64_t g_lastHash;
static int      g_frames, g_changed, g_failed, g_wantNext;
static double   g_readyAt;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint64_t hash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i += 7) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

static void onBuffer(void* d, struct hyprland_toplevel_export_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    g_fmt = fmt, g_w = w, g_h = h, g_stride = stride;
}
static void onDamage(void* d, struct hyprland_toplevel_export_frame_v1* f, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {}
static void onFlags(void* d, struct hyprland_toplevel_export_frame_v1* f, uint32_t flags) {}
static void onDmabuf(void* d, struct hyprland_toplevel_export_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h) {}

static void onBufferDone(void* d, struct hyprland_toplevel_export_frame_v1* f) {
    size_t size = (size_t)g_stride * g_h;
    if (size != g_size) {
        if (g_buf) wl_buffer_destroy(g_buf), munmap(g_data, g_size);
        int fd = memfd_create("share-watch", 0);
        if (fd < 0 || ftruncate(fd, size) < 0) exit(1);
        g_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        struct wl_shm_pool* pool = wl_shm_create_pool(g_shm, fd, size);
        g_buf = wl_shm_pool_create_buffer(pool, 0, g_w, g_h, g_stride, g_fmt);
        wl_shm_pool_destroy(pool);
        close(fd);
        g_size = size;
    }
    hyprland_toplevel_export_frame_v1_copy(f, g_buf, 0);
}

static void done(void) {
    hyprland_toplevel_export_frame_v1_destroy(g_frame);
    g_frame    = NULL;
    g_readyAt  = now();
    g_wantNext = 1;
}

static void onReady(void* d, struct hyprland_toplevel_export_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns) {
    uint64_t h = hash(g_data, g_size);
    g_frames++;
    if (h != g_lastHash) g_changed++;
    g_lastHash = h;
    done();
}

static void onFailed(void* d, struct hyprland_toplevel_export_frame_v1* f) {
    g_failed++;
    done();
}

static const struct hyprland_toplevel_export_frame_v1_listener frameListener = {
    .buffer = onBuffer, .damage = onDamage, .flags = onFlags, .ready = onReady, .failed = onFailed,
    .linux_dmabuf = onDmabuf, .buffer_done = onBufferDone,
};

static void onGlobal(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    if (!strcmp(iface, wl_shm_interface.name)) g_shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    if (!strcmp(iface, hyprland_toplevel_export_manager_v1_interface.name))
        g_mgr = wl_registry_bind(r, name, &hyprland_toplevel_export_manager_v1_interface, 1);
}
static void onGlobalRemove(void* d, struct wl_registry* r, uint32_t name) {}
static const struct wl_registry_listener registryListener = {onGlobal, onGlobalRemove};

int main(int argc, char** argv) {
    if (argc < 3) return fprintf(stderr, "share-watch <0xaddress> <seconds> [fps]\n"), 2;
    g_handle      = (uint32_t)(strtoull(argv[1], NULL, 16) & 0xFFFFFFFF);
    double secs   = atof(argv[2]);
    double period = 1.0 / (argc > 3 ? atof(argv[3]) : 60.0);

    struct wl_display* dpy = wl_display_connect(NULL);
    if (!dpy) return fprintf(stderr, "no wayland display\n"), 1;
    wl_registry_add_listener(wl_display_get_registry(dpy), &registryListener, NULL);
    wl_display_roundtrip(dpy);
    if (!g_mgr || !g_shm) return fprintf(stderr, "no hyprland_toplevel_export_manager_v1\n"), 1;

    double start = now(), mark = start;
    g_wantNext   = 1;
    g_readyAt    = start - period;
    while (now() - start < secs) {
        if (g_wantNext && now() - g_readyAt >= period) {
            g_wantNext = 0;
            g_frame    = hyprland_toplevel_export_manager_v1_capture_toplevel(g_mgr, 0, g_handle);
            hyprland_toplevel_export_frame_v1_add_listener(g_frame, &frameListener, NULL);
        }
        // wait for events up to 2 ms, so the next frame is asked for on time
        while (wl_display_prepare_read(dpy) != 0)
            wl_display_dispatch_pending(dpy);
        wl_display_flush(dpy);
        struct pollfd pfd = {wl_display_get_fd(dpy), POLLIN, 0};
        if (poll(&pfd, 1, 2) > 0)
            wl_display_read_events(dpy);
        else
            wl_display_cancel_read(dpy);
        wl_display_dispatch_pending(dpy);
        if (now() - mark >= 1.0) {
            printf("%5.1fs  frames %3d  different %3d  failed %d\n", now() - start, g_frames, g_changed, g_failed);
            fflush(stdout);
            g_frames = g_changed = g_failed = 0;
            mark += 1.0;
        }
    }
    return 0;
}

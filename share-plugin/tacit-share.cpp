// tacit-share: a Hyprland plugin that keeps a window shared on its own (a call, a stream) live while it is out of sight.
//
// Hyprland copies a window share's frames when the window's screen commits, and sends a window frame callbacks when it
// draws it. A shared window on a workspace not shown, behind a fullscreen window or in a hidden group tab is not drawn,
// so the share froze on its last frame (Discord 2026-10-22: Tacit's apps and Thyris shared to Discord moved only while
// they were on screen):
//  - a window on a workspace not shown is told it is suspended (xdg_toplevel), and Chromium, Qt and GTK stop drawing;
//  - its commits damage nothing, so its screen does not commit and no share frame is copied;
//  - only a copied share frame sends it a frame callback, so it waits for one that never comes.
//
// For as long as a window share runs, the plugin keeps that window unsuspended, sends it the frame callbacks Hyprland
// does not, and asks its screen for a frame when it commits, so Hyprland copies it. When the share ends, Hyprland's own
// suspended states come back. Windows not shared are never touched.
//
// A share runs while its consumer asks for frames: Hyprland's portal shares windows through hyprland-toplevel-export,
// whose sessions outlive the share, so an open session alone does not mean a share.
//
// Hyprland's function hooks are x86-only (udis86); this works through the event bus, so it runs on aarch64 too.
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <aquamarine/output/Output.hpp>
// the sessions and their pending frames are private to the manager
#define private public
#include <hyprland/src/managers/screenshare/ScreenshareManager.hpp>
#undef private

#include <algorithm>
#include <chrono>
#include <vector>

using namespace Desktop::View;

struct SHeld {
    PHLWINDOWREF            window;
    WP<CWLCallbackResource> waiting; // the frame callback it was waiting on at the last tick
    CHyprSignalListener     commit;
};

static std::vector<SHeld>               g_held;
static SP<CEventLoopTimer>              g_tick;
static std::vector<CHyprSignalListener> g_listeners;

// windows whose share runs: the session is open and its consumer is asking for frames (one waits to be copied) or got
// one in the last half second (Hyprland's own m_sharing)
static std::vector<PHLWINDOW> sharedWindows() {
    std::vector<PHLWINDOW> out;
    const auto&            MGR = Screenshare::mgr();
    if (!MGR)
        return out;
    for (const auto& s : MGR->m_sessions) {
        if (!s || s->m_stopped || s->m_type != Screenshare::SHARE_WINDOW)
            continue;
        const auto W = s->m_window.lock();
        if (!W || !W->mapped() || std::ranges::find(out, W) != out.end())
            continue;
        const bool ASKING = s->m_sharing || std::ranges::any_of(MGR->m_pendingFrames, [&](const auto& f) { return f && !f->done() && f->m_session.get() == s.get(); });
        if (ASKING)
            out.push_back(W);
    }
    return out;
}

// not on screen: Hyprland neither draws it nor sends it frame callbacks
static bool unseen(const PHLWINDOW& w) {
    return w->isHidden() || !g_pHyprRenderer->shouldRenderWindow(w) || w->presentation().alphaValue(WINDOW_ALPHA_FADE) * w->presentation().alphaValue(WINDOW_ALPHA_FULLSCREEN) == 0.F;
}

static SP<CWLSurfaceResource> surfaceOf(const PHLWINDOW& w) {
    const auto WL = w->wlSurface();
    return WL ? WL->resource() : nullptr;
}

static void keepAwake() {
    for (const auto& h : g_held)
        if (const auto W = h.window.lock())
            W->setSuspended(false);
}

// Hyprland suspends the windows of a workspace no longer shown right after these events or right before them; the
// configure that tells the app goes out from the idle queue, so keeping the window awake both now and from that queue
// means the app is never told
static void keepAwakeAround() {
    if (g_held.empty())
        return;
    keepAwake();
    g_pEventLoopManager->doLater([] { keepAwake(); });
}

// a shared window off screen committed: its screen commits too, so Hyprland copies the share frame
static void onCommit(const PHLWINDOWREF& ref) {
    const auto W = ref.lock();
    if (!W || !unseen(W))
        return;
    if (const auto M = W->m_monitor.lock(); M && M->m_output)
        M->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
}

static void armTick() {
    if (g_held.empty()) {
        g_tick->updateTimeout(std::nullopt);
        return;
    }
    float hz = 60.F;
    for (const auto& h : g_held)
        if (const auto W = h.window.lock(); W && W->m_monitor)
            hz = std::max(hz, W->m_monitor->m_refreshRate);
    g_tick->updateTimeout(std::chrono::microseconds(sc<long>(1'000'000.F / hz)));
}

static void refresh() {
    const auto SHARED   = sharedWindows();
    bool       released = false;
    std::erase_if(g_held, [&](const SHeld& h) {
        const auto W    = h.window.lock();
        const bool GONE = !W || std::ranges::find(SHARED, W) == SHARED.end();
        if (GONE && W)
            Log::logger->log(Log::INFO, "tacit-share: {} no longer shared", W->metadata().appID());
        released |= GONE;
        return GONE;
    });
    for (const auto& W : SHARED) {
        if (std::ranges::any_of(g_held, [&](const SHeld& h) { return h.window.lock() == W; }))
            continue;
        const auto SURF = surfaceOf(W);
        if (!SURF)
            continue;
        auto& h  = g_held.emplace_back();
        h.window = W;
        h.commit = SURF->m_events.commit.listen([ref = PHLWINDOWREF{W}] { onCommit(ref); });
        Log::logger->log(Log::INFO, "tacit-share: keeping {} live while it is shared", W->metadata().appID());
    }
    // Hyprland's own states for every window, then the shared ones awake again
    if (released)
        Desktop::globalWindowController()->updateSuspendedStates();
    keepAwake();
    armTick();
}

// once a refresh while a window is shared: the share still runs, and an unseen window gets the frame callbacks its
// screen would have sent
static void tick() {
    refresh();
    const auto NOW = Time::steadyNow();
    for (auto& h : g_held) {
        const auto W    = h.window.lock();
        const auto M    = W ? W->m_monitor.lock() : nullptr;
        const auto SURF = W ? surfaceOf(W) : nullptr;
        if (!M || !SURF || !unseen(W)) {
            h.waiting.reset();
            continue;
        }
        // a FIFO barrier lifts when the content is presented, which an unseen window's never is
        SURF->breadthfirst([](SP<CWLSurfaceResource> s, const Vector2D&, void*) { s->m_stateQueue.unlockFirst(LOCK_REASON_FIFO); }, nullptr);

        const auto& CBS   = SURF->m_current.callbacks;
        const auto  FRONT = CBS.empty() ? nullptr : CBS.front();
        // on a workspace not shown, a copied share frame sends the callback, so only one that has already waited a
        // whole tick is sent here; behind a fullscreen window nothing sends it (Hyprland holds the share frame's back
        // while the window counts as shown), so it goes at once
        if (!FRONT || (!g_pHyprRenderer->shouldRenderWindow(W) && h.waiting.lock() != FRONT)) {
            h.waiting = FRONT;
            continue;
        }
        h.waiting.reset();
        SURF->breadthfirst([&](SP<CWLSurfaceResource> s, const Vector2D&, void*) { s->presentFeedback(NOW, M, true); }, nullptr);
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_tick = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) { tick(); }, nullptr);
    g_pEventLoopManager->addTimer(g_tick);

    auto& ev = Event::bus()->m_events;
    g_listeners.emplace_back(ev.screenshare.state.listen([](bool, uint8_t, const std::string&) { refresh(); }));
    g_listeners.emplace_back(ev.workspace.active.listen([](PHLWORKSPACE) { keepAwakeAround(); }));
    g_listeners.emplace_back(ev.workspace.specialActive.listen([](PHLWORKSPACE, PHLMONITOR) { keepAwakeAround(); }));
    g_listeners.emplace_back(ev.workspace.moveToMonitor.listen([](PHLWORKSPACE, PHLMONITOR) { keepAwakeAround(); }));
    g_listeners.emplace_back(ev.window.moveToWorkspace.listen([](PHLWINDOW, PHLWORKSPACE) { keepAwakeAround(); }));

    // loaded while a share runs (a config reload swaps plugins)
    refresh();
    return {"tacit-share", "Keeps a window shared on its own live while it is out of sight", "Tacit", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_listeners.clear();
    const bool HELD = !g_held.empty();
    g_held.clear();
    if (HELD)
        Desktop::globalWindowController()->updateSuspendedStates();
    if (g_tick) {
        g_pEventLoopManager->removeTimer(g_tick);
        g_tick.reset();
    }
}

// keep the inline client-hash export emitted
static const char* const g_clientHash = __hyprland_api_get_client_hash();

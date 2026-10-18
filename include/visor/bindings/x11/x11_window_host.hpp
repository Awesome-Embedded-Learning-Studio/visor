#pragma once

#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

#include <X11/Xlib.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace visor::host::x11 {

/**
 * @brief The first real host: an X window carrying the table's three entries.
 *
 * open() connects a display, creates a canvas-sized window and returns a host
 * whose table talks to it. flush paints each region into a shadow frame (the
 * canvas packing) and XPutImage's it to the window, so an Expose replays the
 * shadow without the core repainting anything: the host is the screen's
 * memory, same stance as the offscreen double. poll_event drains the X queue:
 * pointer and key words go through the B20 translation, Expose replays the
 * shadow, and the window manager's close request turns into close_requested()
 * instead of an event. clock_ms counts steady milliseconds.
 *
 * open() failing is a returned empty optional, never a crash: a missing
 * display is an answer, not an emergency. The window is pinned to the
 * scaled canvas size -- the canvas is compile-time shaped, a resize would
 * only add area nobody paints.
 */
class X11WindowHost {
  public:
    /**
     * @brief Connects a display and creates the window. display_name follows
     *        XOpenDisplay: nullptr reads the environment's DISPLAY.
     *
     * scale blows the picture up at the last step: each canvas pixel lands
     * as a scale-by-scale block on the server, so an OLED-sized canvas is
     * readable on a desktop display. The factor must be a whole number on
     * purpose -- a fractional zoom makes some rows fatter than others, and
     * the 1bpp block look is this machine's own aesthetic anyway. The core
     * and the shadow stay 1:1; scaling touches only the window and the
     * final expand. A scale below 1 returns the empty optional, same line
     * as a missing display.
     */
    [[nodiscard]] static std::optional<X11WindowHost> open(const char* display_name, int width,
                                                           int height, int scale = 1) noexcept;

    ~X11WindowHost();
    X11WindowHost(X11WindowHost&& other) noexcept;
    X11WindowHost& operator=(X11WindowHost&& other) noexcept;
    X11WindowHost(const X11WindowHost&) = delete;
    X11WindowHost& operator=(const X11WindowHost&) = delete;

    /// The table a canvas submits to; handing this to the core is all a host is.
    [[nodiscard]] HostTable table() noexcept;

    /// The window manager's close word arrived: the loop's cue to leave.
    [[nodiscard]] bool close_requested() const noexcept;

    /// Successful region submissions, full frames included.
    [[nodiscard]] int flush_count() const noexcept;

    /// Refused submissions: wrong format, null pixels, an empty region, or a
    /// rectangle leaving the window. Same refusal line as the offscreen host.
    [[nodiscard]] int rejected_count() const noexcept;

    /// The X window id, for judgments and tools that read the screen back.
    [[nodiscard]] Window window() const noexcept;

    /// The display connection, for judgments that put words into this host's
    /// own queue (XPutBackEvent): this Xorg swallows XSendEvent even to the
    /// sender, so the in-process seam is the honest one.
    [[nodiscard]] Display* display() const noexcept;

  private:
    X11WindowHost() = default;

    static void flush_thunk(void* context, const FlushRegion& region) noexcept;
    static bool poll_event_thunk(void* context, Event& out) noexcept;
    static std::uint64_t clock_thunk(void* context) noexcept;
    void handle_flush(const FlushRegion& region) noexcept;
    bool poll_one(Event& out) noexcept;
    void paint_shadow(const FlushRegion& region) noexcept;
    void put_region(base::RectI area) noexcept;
    void release() noexcept;

    Display* display_ = nullptr;
    Window window_ = None;
    GC gc_ = nullptr;
    Atom wm_protocols_atom_ = None;
    Atom wm_delete_atom_ = None;
    int depth_ = 0;
    std::uint32_t lit_word_ = 0;
    std::uint32_t red_mask_ = 0;
    std::uint32_t green_mask_ = 0;
    std::uint32_t blue_mask_ = 0;
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    bool close_requested_ = false;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    std::vector<std::uint8_t> shadow_;    // the screen's memory, canvas packing
    std::vector<std::uint32_t> expanded_; // one region's worth of 32bpp words
};

} // namespace visor::host::x11

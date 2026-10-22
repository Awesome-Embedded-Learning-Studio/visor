#pragma once

#include "visor/base/containers/ring_queue.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Wayland's C types are opaque pointers on this side; only the implementation
// includes the protocol headers.
struct wl_buffer;
struct wl_compositor;
struct wl_display;
struct wl_keyboard;
struct wl_pointer;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;

namespace visor::host::wayland {

/**
 * @brief The second real host: a Wayland window carrying the table's three
 *        entries, the X11 window host's own stance on a new protocol.
 *
 * open() connects a display, builds a canvas-sized xdg toplevel and returns a
 * host whose table talks to it. The pixel half works like the X11 one: flush
 * paints each region into a shadow frame (the canvas packing), the shadow
 * words expand into a window-sized shared-memory buffer (XRGB8888, each canvas
 * pixel a scale-by-scale block), and one attach-damage-commit hands the buffer
 * to the compositor. Wayland takes no 1bpp buffers, so Mono1 and RGB565 expand
 * on the way out, the same expansion every color chapter speaks. Two buffers
 * rotate, and a flush that finds both still held by the compositor waits for
 * the release word -- the client must not redraw a buffer it has not been
 * given back.
 *
 * poll_event drains the display queue into the host's own fixed event feed:
 * pointer and key words go through the evdev-line translation, the wheel
 * accumulates its discrete steps until the pointer's frame word closes the
 * group, and the toplevel's close word turns into close_requested() instead
 * of an event. clock_ms counts steady milliseconds.
 *
 * open() failing is a returned empty optional, never a crash: a missing
 * display is an answer, not an emergency. The window is pinned to the scaled
 * canvas size -- the canvas is compile-time shaped, and a resize the
 * compositor suggests is acknowledged and ignored, because a new size would
 * only add area nobody paints.
 */
class WaylandWindowHost {
  public:
    /// Submissions beyond the first wait for a release word, so a compositor
    /// that holds both buffers back cannot make the flush path reenter.
    static constexpr std::size_t kFeedCapacity = 16;

    /**
     * @brief Connects a display and creates the window. display_name is the
     *        socket to connect to; nullptr reads the environment's
     *        WAYLAND_DISPLAY, the same default wl_display_connect honors.
     *
     * scale blows the picture up at the last step: each canvas pixel lands as
     * a scale-by-scale block in the shared-memory buffer, so an OLED-sized
     * canvas is readable on a desktop display. The factor must be a whole
     * number on purpose -- the same line the X11 window host drew first. A
     * scale below 1 returns the empty optional, same as a missing display.
     * format is the wire format the window accepts; a region in any other
     * format is refused and counted, the offscreen host's own line.
     */
    [[nodiscard]] static std::optional<WaylandWindowHost>
    open(const char* display_name, int width, int height, int scale = 1,
         PixelFormat format = PixelFormat::Mono1) noexcept;

    ~WaylandWindowHost();
    WaylandWindowHost(WaylandWindowHost&& other) noexcept;
    WaylandWindowHost& operator=(WaylandWindowHost&& other) noexcept;
    WaylandWindowHost(const WaylandWindowHost&) = delete;
    WaylandWindowHost& operator=(const WaylandWindowHost&) = delete;

    /// The table a canvas submits to; handing this to the core is all a host
    /// is. The table carries this object's address as its context, so a table
    /// taken before a move strands that address: take the table again after
    /// moving the host, or do not move it while a table is in use.
    [[nodiscard]] HostTable table() noexcept;

    /// The compositor's close word arrived: the loop's cue to leave.
    [[nodiscard]] bool close_requested() const noexcept;

    /// Successful region submissions, full frames included.
    [[nodiscard]] int flush_count() const noexcept;

    /// Refused submissions: wrong format, null pixels, an empty region, or a
    /// rectangle leaving the window. Same refusal line as the offscreen host.
    [[nodiscard]] int rejected_count() const noexcept;

    /// Events the feed refused since construction. A full feed drops the
    /// newcomer and counts it, the fake session's own line.
    [[nodiscard]] int dropped_event_count() const noexcept;

    /// The words of the window-sized buffer the last flush filled, XRGB8888
    /// packing. Wayland offers no readback from the compositor, so judgments
    /// that need pixels read what the host itself wrote.
    [[nodiscard]] const std::uint32_t* submitted_words() const noexcept;

    /// The row stride of submitted_words(), in words, for judgments walking
    /// the buffer.
    [[nodiscard]] std::size_t window_stride_words() const noexcept;

    /// The display connection, for judgments that watch the protocol's own
    /// words (a frame callback, say) on the live link.
    [[nodiscard]] wl_display* display() const noexcept;

    /// The surface, for judgments that hang their own callbacks on it.
    [[nodiscard]] wl_surface* surface() const noexcept;

  private:
    WaylandWindowHost() = default;

    /// Owns the protocol's C listener tables and the callbacks they name. It
    /// lives only in the implementation, where the protocol headers are
    /// visible; the class grants it access because every listener is this
    /// host's own wiring, not an outsider.
    struct Wiring;
    friend struct Wiring;

    static void flush_thunk(void* context, const FlushRegion& region) noexcept;
    static bool poll_event_thunk(void* context, Event& out) noexcept;
    static std::uint64_t clock_thunk(void* context) noexcept;
    void handle_flush(const FlushRegion& region) noexcept;
    bool poll_one(Event& out) noexcept;
    void paint_shadow(const FlushRegion& region) noexcept;
    /// Expands one shadow area into the free shared-memory buffer and commits
    /// it; returns false when no buffer ever comes back.
    bool put_region(base::RectI area) noexcept;
    [[nodiscard]] std::uint32_t canvas_pixel_word(const std::uint8_t* row,
                                                  std::size_t x) const noexcept;
    void release() noexcept;

    /// Re-adds every protocol listener with this address as its data, the
    /// move half's own repair: proxies carry the address of the object they
    /// were born in, and a moved host must answer on its new one.
    void rewire() noexcept;

    struct Buffer {
        wl_buffer* buffer = nullptr;
        std::uint32_t* words = nullptr; // the mmap'd window frame, XRGB8888
        bool free = true;               // the compositor released it
        std::uint64_t shadow_epoch = 0; // the shadow version this image matches
    };

    wl_display* display_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    wl_seat* seat_ = nullptr;
    std::uint32_t seat_version_ = 0; // the version actually bound, for the
                                     // versioned goodbye (release vs destroy)
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    void* pool_map_ = nullptr; // MAP_SHARED over both buffers
    std::size_t pool_bytes_ = 0;
    std::array<Buffer, 2> buffers_{};
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    PixelFormat format_ = PixelFormat::Mono1;
    base::PointI pointer_pos_;
    host::Modifiers modifiers_{};
    int pending_wheel_dx_ = 0;
    int pending_wheel_dy_ = 0;
    bool configured_ = false; // the compositor answered the first configure
    bool close_requested_ = false;
    /// Bumped by every paint_shadow; a buffer whose epoch lags carries a
    /// stale image and must be repainted whole. Zero means "never met the
    /// shadow", which the first epoch (one) never equals.
    std::uint64_t shadow_epoch_ = 1;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    int dropped_event_count_ = 0;
    const std::uint32_t* submitted_ = nullptr;
    base::container::RingQueue<Event, kFeedCapacity> feed_{};
    std::vector<std::uint8_t> shadow_;    // the screen's memory, format_ packing
    std::vector<std::uint32_t> expanded_; // one area's worth of 32bpp words
};

} // namespace visor::host::wayland

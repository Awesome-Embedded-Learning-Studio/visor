#include "visor/bindings/wayland/wayland_window_host.hpp"

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/wayland/event_wire.hpp"
#include "visor/canvas/pixel_convert.hpp"
#include "visor/canvas/pixel_traits.hpp"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>
#include <utility>

namespace visor::host::wayland {
namespace {

// The panel is dark, the ink is lit: white words on black, the X11 host's own
// ink pair said in XRGB8888. The X byte is padding no compositor reads, so
// both dark words display the same black; the pair keeps the byte image
// honest for judgments that read the buffer back.
constexpr std::uint32_t kXrgbLit = 0xffff'ffff;
constexpr std::uint32_t kXrgbDark = 0xff00'0000;

// The versions the host binds, each capped at what the compositor advertised.
// The seat wants 5 for the pointer's frame and axis_discrete words; the rest
// speak everything they need at 1. A compositor older than seat 5 still runs
// the host, only without wheel words -- the discrete pair never arrives there.
constexpr std::uint32_t kCompositorVersion = 1;
constexpr std::uint32_t kShmVersion = 1;
constexpr std::uint32_t kWmBaseVersion = 1;
constexpr std::uint32_t kSeatVersion = 5;

// How long the flush path waits for a buffer release on one monotonic clock.
// A healthy compositor answers inside a frame; the bound only turns a dead
// one into a refused flush instead of a stuck one, and no count of dispatch
// words can say that, because one dispatch alone can wait forever.
constexpr auto kReleaseBudget = std::chrono::milliseconds(1000);

// The same shape for open()'s wait on the first configure.
constexpr auto kConfigureBudget = std::chrono::milliseconds(1000);

// One wait slice: short enough to notice the deadline often, long enough not
// to spin.
constexpr timespec kWaitSlice{.tv_sec = 0, .tv_nsec = 10'000'000};

[[nodiscard]] std::uint32_t bind_version(std::uint32_t advertised, std::uint32_t wanted) noexcept {
    return advertised < wanted ? advertised : wanted;
}

} // namespace

/// The protocol's own wiring: every listener the compositor calls through,
/// gathered where the protocol headers are visible. The listeners write the
/// host's state directly -- they are the host's own hands, not visitors.
struct WaylandWindowHost::Wiring {
    static void offer(WaylandWindowHost& host, std::optional<Event> word) noexcept {
        if (!word.has_value()) {
            return;
        }
        if (!host.feed_.push(*word)) {
            ++host.dropped_event_count_;
        }
    }

    static void registry_global(void* data, wl_registry* registry, std::uint32_t name,
                                const char* interface, std::uint32_t version) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        if (std::strcmp(interface, "wl_compositor") == 0) {
            host->compositor_ = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface,
                                 bind_version(version, kCompositorVersion)));
        } else if (std::strcmp(interface, "wl_shm") == 0) {
            host->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface,
                                                               bind_version(version, kShmVersion)));
        } else if (std::strcmp(interface, "xdg_wm_base") == 0) {
            host->wm_base_ = static_cast<xdg_wm_base*>(wl_registry_bind(
                registry, name, &xdg_wm_base_interface, bind_version(version, kWmBaseVersion)));
        } else if (std::strcmp(interface, "wl_seat") == 0) {
            const std::uint32_t bound = bind_version(version, kSeatVersion);
            host->seat_version_ = bound;
            host->seat_ =
                static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bound));
        }
    }

    static void registry_global_remove([[maybe_unused]] void* data,
                                       [[maybe_unused]] wl_registry* registry,
                                       [[maybe_unused]] std::uint32_t name) noexcept {
        // A global going away mid-session is the compositor dying; the next
        // dispatch turns that into an error the host surfaces by its own
        // lights.
    }

    static void wm_base_ping([[maybe_unused]] void* data, xdg_wm_base* wm_base,
                             std::uint32_t serial) noexcept {
        // The compositor asks whether the client is alive; answering is all
        // this word wants, and nothing else moves.
        xdg_wm_base_pong(wm_base, serial);
    }

    static void surface_configure(void* data, xdg_surface* xdg_surface,
                                  [[maybe_unused]] std::uint32_t serial) noexcept {
        // The compositor may suggest a size in its configure words; the window
        // is pinned to the canvas size at birth, so the answer is always the
        // same: acknowledge and keep the canvas. The first configure also
        // unblocks open(), whose wait loop watches the flag through round
        // trips.
        static_cast<WaylandWindowHost*>(data)->configured_ = true;
        xdg_surface_ack_configure(xdg_surface, serial);
    }

    static void toplevel_close(void* data, [[maybe_unused]] xdg_toplevel* toplevel) noexcept {
        static_cast<WaylandWindowHost*>(data)->close_requested_ = true;
    }

    static void toplevel_configure([[maybe_unused]] void* data,
                                   [[maybe_unused]] xdg_toplevel* toplevel,
                                   [[maybe_unused]] std::int32_t width,
                                   [[maybe_unused]] std::int32_t height,
                                   [[maybe_unused]] wl_array* states) noexcept {
        // Sizing travels with the xdg_surface configure words; the states
        // array (maximized, fullscreen, ...) names things the host never
        // asks for. Nothing to do here.
    }

    static void toplevel_wm_capabilities([[maybe_unused]] void* data,
                                         [[maybe_unused]] xdg_toplevel* toplevel,
                                         [[maybe_unused]] wl_array* capabilities) noexcept {
        // What the compositor lets this window do (maximize, fullscreen,
        // close) is its business; the host only ever draws its canvas and
        // answers close.
    }

    static void buffer_release(void* data, wl_buffer* buffer) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        for (auto& slot : host->buffers_) {
            if (slot.buffer == buffer) {
                slot.free = true;
            }
        }
    }

    static void seat_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        const bool pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
        const bool keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
        if (pointer && host->pointer_ == nullptr) {
            host->pointer_ = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(host->pointer_, &kPointer, host);
        } else if (!pointer && host->pointer_ != nullptr) {
            wl_pointer_release(host->pointer_);
            host->pointer_ = nullptr;
        }
        if (keyboard && host->keyboard_ == nullptr) {
            host->keyboard_ = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(host->keyboard_, &kKeyboard, host);
        } else if (!keyboard && host->keyboard_ != nullptr) {
            wl_keyboard_release(host->keyboard_);
            host->keyboard_ = nullptr;
        }
    }

    static void seat_name([[maybe_unused]] void* data, [[maybe_unused]] wl_seat* seat,
                          [[maybe_unused]] const char* name) noexcept {
        // Which seat this is (seat0, a drawing tablet's own) carries nothing
        // the host acts on; the words of one seat are the words of another.
    }

    static void pointer_enter(void* data, wl_pointer* pointer, std::uint32_t serial,
                              [[maybe_unused]] wl_surface* surface, wl_fixed_t sx,
                              wl_fixed_t sy) noexcept {
        // Enter is the one word that both names a position and hands over the
        // serial a set_cursor answer needs, so it doubles as a motion word.
        // The desktop paints its own pointer on its own canvas; the
        // compositor's is asked away.
        auto* host = static_cast<WaylandWindowHost*>(data);
        host->pointer_pos_ =
            base::PointI{wl_fixed_to_int(sx) / host->scale_, wl_fixed_to_int(sy) / host->scale_};
        wl_pointer_set_cursor(pointer, serial, nullptr, 0, 0);
        offer(*host, translate_motion(host->pointer_pos_));
    }

    static void pointer_leave([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer,
                              [[maybe_unused]] std::uint32_t serial,
                              [[maybe_unused]] wl_surface* surface) noexcept {
        // Leaving the surface carries no position, so there is no word to say.
    }

    static void pointer_motion(void* data, [[maybe_unused]] wl_pointer* pointer,
                               [[maybe_unused]] std::uint32_t time, wl_fixed_t sx,
                               wl_fixed_t sy) noexcept {
        // Pointer words speak window pixels, the core speaks canvas pixels:
        // divide back before the word is built, floor and all.
        auto* host = static_cast<WaylandWindowHost*>(data);
        host->pointer_pos_ =
            base::PointI{wl_fixed_to_int(sx) / host->scale_, wl_fixed_to_int(sy) / host->scale_};
        offer(*host, translate_motion(host->pointer_pos_));
    }

    static void pointer_button(void* data, [[maybe_unused]] wl_pointer* pointer,
                               [[maybe_unused]] std::uint32_t serial,
                               [[maybe_unused]] std::uint32_t time, std::uint32_t button,
                               std::uint32_t state) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        offer(*host, translate_button(button, state == WL_POINTER_BUTTON_STATE_PRESSED,
                                      host->pointer_pos_, host->modifiers_));
    }

    static void pointer_axis([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer,
                             [[maybe_unused]] std::uint32_t time,
                             [[maybe_unused]] std::uint32_t axis,
                             [[maybe_unused]] wl_fixed_t value) noexcept {
        // Continuous scroll lengths stay unread: the core counts detents, and
        // the discrete word beside this one carries them.
    }

    static void pointer_axis_discrete(void* data, [[maybe_unused]] wl_pointer* pointer,
                                      [[maybe_unused]] std::uint32_t axis,
                                      std::int32_t discrete) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            host->pending_wheel_dx_ += discrete;
        } else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            host->pending_wheel_dy_ += discrete;
        }
    }

    static void pointer_axis_source([[maybe_unused]] void* data,
                                    [[maybe_unused]] wl_pointer* pointer,
                                    [[maybe_unused]] std::uint32_t source) noexcept {
        // Wheel, touchpad or finger: the host reads detents either way.
    }

    static void pointer_axis_stop([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer,
                                  [[maybe_unused]] std::uint32_t time,
                                  [[maybe_unused]] std::uint32_t axis) noexcept {
        // Kinetic scrolling's end word; the host has no kinetics to stop.
    }

    static void pointer_frame(void* data, [[maybe_unused]] wl_pointer* pointer) noexcept {
        // The frame closes the group; a wheel that gathered steps inside it
        // becomes one word here, the protocol's own grouping.
        auto* host = static_cast<WaylandWindowHost*>(data);
        offer(*host, translate_wheel(host->pending_wheel_dx_, host->pending_wheel_dy_,
                                     host->pointer_pos_, host->modifiers_));
        host->pending_wheel_dx_ = 0;
        host->pending_wheel_dy_ = 0;
    }

    static void keyboard_keymap([[maybe_unused]] void* data, [[maybe_unused]] wl_keyboard* keyboard,
                                [[maybe_unused]] std::uint32_t format, std::int32_t fd,
                                [[maybe_unused]] std::uint32_t size) noexcept {
        // The host reads keys as raw evdev codes, never through a keymap, so
        // the mapped file the compositor hands over is closed unread --
        // descriptors are the compositor's gift, not the host's to keep.
        ::close(fd);
    }

    static void keyboard_enter([[maybe_unused]] void* data, [[maybe_unused]] wl_keyboard* keyboard,
                               [[maybe_unused]] std::uint32_t serial,
                               [[maybe_unused]] wl_surface* surface,
                               [[maybe_unused]] wl_array* keys) noexcept {
        // Which keys were already down on entry is state the core rebuilds
        // from its own words; nothing to hand over.
    }

    static void keyboard_leave([[maybe_unused]] void* data, [[maybe_unused]] wl_keyboard* keyboard,
                               [[maybe_unused]] std::uint32_t serial,
                               [[maybe_unused]] wl_surface* surface) noexcept {}

    static void keyboard_key(void* data, [[maybe_unused]] wl_keyboard* keyboard,
                             [[maybe_unused]] std::uint32_t serial,
                             [[maybe_unused]] std::uint32_t time, std::uint32_t key,
                             std::uint32_t state) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        offer(*host, translate_key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED, host->modifiers_));
    }

    static void keyboard_modifiers(void* data, [[maybe_unused]] wl_keyboard* keyboard,
                                   [[maybe_unused]] std::uint32_t serial, std::uint32_t depressed,
                                   [[maybe_unused]] std::uint32_t latched,
                                   [[maybe_unused]] std::uint32_t locked,
                                   [[maybe_unused]] std::uint32_t group) noexcept {
        // The depressed mask is the live one; latched and locked describe
        // sticky states the core's four flags have no room for.
        static_cast<WaylandWindowHost*>(data)->modifiers_ = translate_modifiers(depressed);
    }

    static void keyboard_repeat([[maybe_unused]] void* data, [[maybe_unused]] wl_keyboard* keyboard,
                                [[maybe_unused]] std::int32_t rate,
                                [[maybe_unused]] std::int32_t delay) noexcept {
        // Auto-repeat is the core's layer to grow later (the evdev line
        // again); the host lets the compositor's settings pass by.
    }

    static const wl_registry_listener kRegistry;
    static const xdg_wm_base_listener kWmBase;
    static const xdg_surface_listener kXdgSurface;
    static const xdg_toplevel_listener kToplevel;
    static const wl_buffer_listener kBuffer;
    static const wl_seat_listener kSeat;
    static const wl_pointer_listener kPointer;
    static const wl_keyboard_listener kKeyboard;
};

const wl_registry_listener WaylandWindowHost::Wiring::kRegistry = {
    .global = Wiring::registry_global,
    .global_remove = Wiring::registry_global_remove,
};

const xdg_wm_base_listener WaylandWindowHost::Wiring::kWmBase = {
    .ping = Wiring::wm_base_ping,
};

const xdg_surface_listener WaylandWindowHost::Wiring::kXdgSurface = {
    .configure = Wiring::surface_configure,
};

const xdg_toplevel_listener WaylandWindowHost::Wiring::kToplevel = {
    .configure = Wiring::toplevel_configure,
    .close = Wiring::toplevel_close,
    .configure_bounds = nullptr, // never drawn from: bounds only advise sizing
    .wm_capabilities = Wiring::toplevel_wm_capabilities,
};

const wl_buffer_listener WaylandWindowHost::Wiring::kBuffer = {
    .release = Wiring::buffer_release,
};

const wl_seat_listener WaylandWindowHost::Wiring::kSeat = {
    .capabilities = Wiring::seat_capabilities,
    .name = Wiring::seat_name,
};

const wl_pointer_listener WaylandWindowHost::Wiring::kPointer = {
    .enter = Wiring::pointer_enter,
    .leave = Wiring::pointer_leave,
    .motion = Wiring::pointer_motion,
    .button = Wiring::pointer_button,
    .axis = Wiring::pointer_axis,
    .frame = Wiring::pointer_frame,
    .axis_source = Wiring::pointer_axis_source,
    .axis_stop = Wiring::pointer_axis_stop,
    .axis_discrete = Wiring::pointer_axis_discrete,
    // The seat binds at 5, so the high-resolution words never arrive; null is
    // the honest slot for them.
    .axis_value120 = nullptr,
    .axis_relative_direction = nullptr,
    .warp = nullptr,
};

const wl_keyboard_listener WaylandWindowHost::Wiring::kKeyboard = {
    .keymap = Wiring::keyboard_keymap,
    .enter = Wiring::keyboard_enter,
    .leave = Wiring::keyboard_leave,
    .key = Wiring::keyboard_key,
    .modifiers = Wiring::keyboard_modifiers,
    .repeat_info = Wiring::keyboard_repeat,
};

std::optional<WaylandWindowHost> WaylandWindowHost::open(const char* display_name, int width,
                                                         int height, int scale,
                                                         PixelFormat format) noexcept {
    if (scale < 1 || width <= 0 || height <= 0) {
        return std::nullopt;
    }
    wl_display* display = wl_display_connect(display_name);
    if (display == nullptr) {
        return std::nullopt;
    }
    WaylandWindowHost host;
    host.display_ = display;

    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &Wiring::kRegistry, &host);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    if (host.compositor_ == nullptr || host.shm_ == nullptr || host.wm_base_ == nullptr) {
        host.release();
        return std::nullopt;
    }
    xdg_wm_base_add_listener(host.wm_base_, &Wiring::kWmBase, &host);
    if (host.seat_ != nullptr) {
        wl_seat_add_listener(host.seat_, &Wiring::kSeat, &host);
    }

    // One shared-memory pool holds both window buffers; the client owns the
    // mapping and the compositor reads through its own copy of the fd, so a
    // memfd gives the pair their memory without a name in the filesystem.
    const std::size_t window_width =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(scale);
    const std::size_t window_height =
        static_cast<std::size_t>(height) * static_cast<std::size_t>(scale);
    const std::size_t stride_bytes = window_width * 4U;
    host.pool_bytes_ = stride_bytes * window_height * std::size_t{2};
    const int fd = ::memfd_create("visor-wayland", 0);
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(host.pool_bytes_)) != 0) {
        if (fd >= 0) {
            ::close(fd);
        }
        host.release();
        return std::nullopt;
    }
    host.pool_map_ = ::mmap(nullptr, host.pool_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (host.pool_map_ == MAP_FAILED) {
        ::close(fd);
        host.pool_map_ = nullptr;
        host.release();
        return std::nullopt;
    }
    wl_shm_pool* pool =
        wl_shm_create_pool(host.shm_, fd, static_cast<std::int32_t>(host.pool_bytes_));
    ::close(fd); // the pool keeps its own reference; the fd is not needed again
    for (std::size_t i = 0; i < host.buffers_.size(); ++i) {
        const std::size_t offset = i * stride_bytes * window_height;
        host.buffers_[i].words = static_cast<std::uint32_t*>(host.pool_map_) + (offset / 4U);
        host.buffers_[i].buffer = wl_shm_pool_create_buffer(
            pool, static_cast<std::int32_t>(offset), static_cast<std::int32_t>(window_width),
            static_cast<std::int32_t>(window_height), static_cast<std::int32_t>(stride_bytes),
            WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(host.buffers_[i].buffer, &Wiring::kBuffer, &host);
    }
    wl_shm_pool_destroy(pool); // the buffers outlive their pool, the protocol's own word

    host.width_ = width;
    host.height_ = height;
    host.scale_ = scale;
    host.format_ = format;
    const std::size_t shadow_stride = row_bytes(format, static_cast<std::size_t>(width));
    host.shadow_.assign(shadow_stride * static_cast<std::size_t>(height), 0);

    host.surface_ = wl_compositor_create_surface(host.compositor_);
    host.xdg_surface_ = xdg_wm_base_get_xdg_surface(host.wm_base_, host.surface_);
    xdg_surface_add_listener(host.xdg_surface_, &Wiring::kXdgSurface, &host);
    host.toplevel_ = xdg_surface_get_toplevel(host.xdg_surface_);
    xdg_toplevel_add_listener(host.toplevel_, &Wiring::kToplevel, &host);
    xdg_toplevel_set_title(host.toplevel_, "visor");
    wl_surface_commit(host.surface_); // the empty commit that asks for the first configure

    wl_display_flush(display); // the commit above must leave before it can be answered
    const auto deadline = std::chrono::steady_clock::now() + kConfigureBudget;
    while (!host.configured_) {
        if (wl_display_dispatch_timeout(display, &kWaitSlice) < 0 ||
            std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
    if (!host.configured_) {
        host.release();
        return std::nullopt;
    }
    // The acknowledged first frame: the whole canvas, still dark, so the
    // compositor holds something real from the very first commit and the
    // flush that follows paints over a known state.
    if (!host.put_region(base::RectI{
            .pos = base::PointI{0, 0},
            .size = base::SizeI{.w = width, .h = height},
        })) {
        host.release();
        return std::nullopt;
    }
    return host;
}

WaylandWindowHost::~WaylandWindowHost() {
    release();
}

WaylandWindowHost::WaylandWindowHost(WaylandWindowHost&& other) noexcept {
    *this = std::move(other);
}

WaylandWindowHost& WaylandWindowHost::operator=(WaylandWindowHost&& other) noexcept {
    if (this != &other) {
        release();
        display_ = other.display_;
        compositor_ = other.compositor_;
        shm_ = other.shm_;
        wm_base_ = other.wm_base_;
        seat_ = other.seat_;
        pointer_ = other.pointer_;
        keyboard_ = other.keyboard_;
        surface_ = other.surface_;
        xdg_surface_ = other.xdg_surface_;
        toplevel_ = other.toplevel_;
        pool_map_ = other.pool_map_;
        pool_bytes_ = other.pool_bytes_;
        buffers_ = other.buffers_;
        width_ = other.width_;
        height_ = other.height_;
        scale_ = other.scale_;
        format_ = other.format_;
        pointer_pos_ = other.pointer_pos_;
        modifiers_ = other.modifiers_;
        pending_wheel_dx_ = other.pending_wheel_dx_;
        pending_wheel_dy_ = other.pending_wheel_dy_;
        configured_ = other.configured_;
        seat_version_ = other.seat_version_;
        shadow_epoch_ = other.shadow_epoch_;
        close_requested_ = other.close_requested_;
        flush_count_ = other.flush_count_;
        rejected_count_ = other.rejected_count_;
        dropped_event_count_ = other.dropped_event_count_;
        submitted_ = other.submitted_;
        feed_ = other.feed_;
        shadow_ = std::move(other.shadow_);
        other.display_ = nullptr;
        other.compositor_ = nullptr;
        other.shm_ = nullptr;
        other.wm_base_ = nullptr;
        other.seat_ = nullptr;
        other.pointer_ = nullptr;
        other.keyboard_ = nullptr;
        other.surface_ = nullptr;
        other.xdg_surface_ = nullptr;
        other.toplevel_ = nullptr;
        other.pool_map_ = nullptr;
        other.pool_bytes_ = 0;
        other.buffers_ = {};
        other.submitted_ = nullptr;
        other.configured_ = false;
        // Every protocol listener still points at the host the proxies were
        // born in -- the moved-from object, already gutted above. Wayland
        // listeners are plain proxy state, so re-adding them re-homes every
        // callback onto this address, the one the compositor's words must
        // reach. Skipping this is not a dangling crash but a worse thing: a
        // compositor happily releasing buffers into the corpse while the
        // living host waits forever for its own buffers back.
        rewire();
    }
    return *this;
}

void WaylandWindowHost::rewire() noexcept {
    // add_listener refuses a second set (the proxies keep the tables they were
    // born with), so the repair goes one level lower: each proxy's user data
    // becomes this address, and the tables' callbacks read it back as their
    // first argument.
    if (wm_base_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(wm_base_), this);
    }
    if (seat_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(seat_), this);
    }
    if (pointer_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(pointer_), this);
    }
    if (keyboard_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(keyboard_), this);
    }
    if (xdg_surface_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(xdg_surface_), this);
    }
    if (toplevel_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(toplevel_), this);
    }
    for (const auto& slot : buffers_) {
        if (slot.buffer != nullptr) {
            wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(slot.buffer), this);
        }
    }
}

void WaylandWindowHost::release() noexcept {
    if (display_ == nullptr) {
        return;
    }
    // An orderly goodbye of our own: destroy the window's pieces first, let
    // the compositor settle, then hand the pool back and close. Buffer
    // destroys are safe even while the compositor still holds them -- the
    // protocol takes a held buffer down with its surface.
    if (toplevel_ != nullptr) {
        xdg_toplevel_destroy(toplevel_);
        toplevel_ = nullptr;
    }
    if (xdg_surface_ != nullptr) {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }
    if (surface_ != nullptr) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
    for (auto& slot : buffers_) {
        if (slot.buffer != nullptr) {
            wl_buffer_destroy(slot.buffer);
            slot.buffer = nullptr;
        }
    }
    if (pool_map_ != nullptr) {
        ::munmap(pool_map_, pool_bytes_);
        pool_map_ = nullptr;
    }
    // release exists from its interface's version on; an older binding must
    // take the plain destroy, or the compositor reads a request the seat
    // never learned.
    if (pointer_ != nullptr) {
        if (seat_version_ >= 3) {
            wl_pointer_release(pointer_);
        } else {
            wl_pointer_destroy(pointer_);
        }
        pointer_ = nullptr;
    }
    if (keyboard_ != nullptr) {
        if (seat_version_ >= 3) {
            wl_keyboard_release(keyboard_);
        } else {
            wl_keyboard_destroy(keyboard_);
        }
        keyboard_ = nullptr;
    }
    if (seat_ != nullptr) {
        if (seat_version_ >= 5) {
            wl_seat_release(seat_);
        } else {
            wl_seat_destroy(seat_);
        }
        seat_ = nullptr;
    }
    if (wm_base_ != nullptr) {
        xdg_wm_base_destroy(wm_base_);
        wm_base_ = nullptr;
    }
    if (shm_ != nullptr) {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }
    if (compositor_ != nullptr) {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }
    wl_display_disconnect(display_);
    display_ = nullptr;
}

HostTable WaylandWindowHost::table() noexcept {
    HostTable table;
    table.flush = &WaylandWindowHost::flush_thunk;
    table.poll_event = &WaylandWindowHost::poll_event_thunk;
    table.clock_ms = &WaylandWindowHost::clock_thunk;
    table.context = this;
    table.info = HostInfo{
        .screen = base::SizeI{.w = width_, .h = height_},
        .format = format_,
    };
    return table;
}

bool WaylandWindowHost::close_requested() const noexcept {
    return close_requested_;
}

int WaylandWindowHost::flush_count() const noexcept {
    return flush_count_;
}

int WaylandWindowHost::rejected_count() const noexcept {
    return rejected_count_;
}

int WaylandWindowHost::dropped_event_count() const noexcept {
    return dropped_event_count_;
}

const std::uint32_t* WaylandWindowHost::submitted_words() const noexcept {
    return submitted_;
}

std::size_t WaylandWindowHost::window_stride_words() const noexcept {
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(scale_);
}

wl_display* WaylandWindowHost::display() const noexcept {
    return display_;
}

wl_surface* WaylandWindowHost::surface() const noexcept {
    return surface_;
}

void WaylandWindowHost::handle_flush(const FlushRegion& region) noexcept {
    const bool inside = region.format == format_ && region.pixels != nullptr &&
                        !region.rect.is_empty() && region.rect.pos.x >= 0 &&
                        region.rect.pos.y >= 0 &&
                        region.rect.pos.x + region.rect.size.w <= width_ &&
                        region.rect.pos.y + region.rect.size.h <= height_;
    if (!inside) {
        ++rejected_count_;
        return;
    }
    paint_shadow(region);
    ++shadow_epoch_;
    if (!put_region(region.rect)) {
        // The shadow moved but no buffer ever came back; the flush reads as
        // refused, and the next flush carries the shadow's whole news, because
        // the epoch has already moved past every buffer.
        ++rejected_count_;
        return;
    }
    ++flush_count_;
}

void WaylandWindowHost::paint_shadow(const FlushRegion& region) noexcept {
    const std::size_t dst_stride = row_bytes(format_, static_cast<std::size_t>(width_));
    if (format_ != PixelFormat::Mono1) {
        // A color region is whole bytes per pixel, so the shadow takes a row copy.
        const std::size_t pixel_bytes = format_ == PixelFormat::RGB565 ? 2U : 4U;
        const std::size_t row_width = static_cast<std::size_t>(region.rect.size.w) * pixel_bytes;
        const std::size_t dst_column = static_cast<std::size_t>(region.rect.pos.x) * pixel_bytes;
        for (int row = 0; row < region.rect.size.h; ++row) {
            const std::uint8_t* src_row =
                region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
            std::uint8_t* dst_row =
                shadow_.data() +
                ((static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) *
                 dst_stride) +
                dst_column;
            std::copy(src_row, src_row + row_width, dst_row);
        }
        return;
    }
    // Pixel-by-pixel, MSB-first, the canvas's own packing. The per-byte fast
    // path lives in the offscreen host; a second shopper is when it moves out.
    // pixels already points at the region's leading byte (the FlushRegion
    // contract), so the row walk starts there; the column index subtracts the
    // region's own leading byte to name the source byte for a global x.
    const std::size_t first_src_byte = static_cast<std::size_t>(region.rect.pos.x) / 8;
    for (int row = 0; row < region.rect.size.h; ++row) {
        const std::uint8_t* src =
            region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
        std::uint8_t* dst =
            shadow_.data() + (static_cast<std::size_t>(region.rect.pos.y + row) * dst_stride);
        for (int col = 0; col < region.rect.size.w; ++col) {
            const std::size_t x =
                static_cast<std::size_t>(region.rect.pos.x) + static_cast<std::size_t>(col);
            const unsigned mask =
                base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)).raw;
            std::uint8_t& byte = dst[x / 8];
            if ((src[(x / 8) - first_src_byte] & mask) != 0) {
                byte = static_cast<std::uint8_t>(byte | mask);
            } else {
                byte = static_cast<std::uint8_t>(byte & ~mask);
            }
        }
    }
}

bool WaylandWindowHost::put_region(base::RectI area) noexcept {
    Buffer* chosen = nullptr;
    for (auto& slot : buffers_) {
        if (slot.free) {
            chosen = &slot;
            break;
        }
    }
    if (chosen == nullptr) {
        // Both buffers are still in the compositor's hands. Blocking here is
        // the honest move for this host's single-threaded callers: the wait
        // ends with a release word, and a compositor that never sends one is
        // a dying display, which the loop exits through as an error.
        const auto deadline = std::chrono::steady_clock::now() + kReleaseBudget;
        while (chosen == nullptr) {
            if (wl_display_dispatch_timeout(display_, &kWaitSlice) < 0) {
                return false;
            }
            for (auto& slot : buffers_) {
                if (slot.free) {
                    chosen = &slot;
                    break;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
        }
        if (chosen == nullptr) {
            return false;
        }
    }

    // A buffer whose image lags the shadow must arrive whole: the shadow is
    // the only truth, and a stale buffer mixes history with the caller's
    // area. A caught-up buffer takes the area alone, the same line the X11
    // host draws between a fill and a put.
    if (chosen->shadow_epoch != shadow_epoch_) {
        area = base::RectI{
            .pos = base::PointI{0, 0},
            .size = base::SizeI{.w = width_, .h = height_},
        };
    }

    // The shadow's rows become 32bpp words, each canvas pixel a
    // scale-by-scale block, then one attach-damage-commit carries the buffer
    // to the compositor.
    const std::size_t canvas_stride = row_bytes(format_, static_cast<std::size_t>(width_));
    const std::size_t window_stride = window_stride_words();
    for (int row = 0; row < area.size.h; ++row) {
        const std::uint8_t* src =
            shadow_.data() + (static_cast<std::size_t>(area.pos.y + row) * canvas_stride);
        for (int block_row = 0; block_row < scale_; ++block_row) {
            const std::size_t out_row =
                (static_cast<std::size_t>(area.pos.y + row) * static_cast<std::size_t>(scale_)) +
                static_cast<std::size_t>(block_row);
            const std::size_t out_x =
                static_cast<std::size_t>(area.pos.x) * static_cast<std::size_t>(scale_);
            std::uint32_t* out = chosen->words + (out_row * window_stride) + out_x;
            for (int col = 0; col < area.size.w; ++col) {
                const std::size_t x =
                    static_cast<std::size_t>(area.pos.x) + static_cast<std::size_t>(col);
                const std::uint32_t word = canvas_pixel_word(src, x);
                for (int stretch = 0; stretch < scale_; ++stretch) {
                    *out++ = word;
                }
            }
        }
    }
    wl_surface_attach(surface_, chosen->buffer, 0, 0);
    wl_surface_damage(surface_, area.pos.x * scale_, area.pos.y * scale_, area.size.w * scale_,
                      area.size.h * scale_);
    wl_surface_commit(surface_);
    wl_display_flush(display_);
    chosen->free = false;
    chosen->shadow_epoch = shadow_epoch_;
    submitted_ = chosen->words;
    return true;
}

std::uint32_t WaylandWindowHost::canvas_pixel_word(const std::uint8_t* row,
                                                   std::size_t x) const noexcept {
    switch (format_) {
        case PixelFormat::Mono1: {
            const unsigned mask =
                base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)).raw;
            return (row[x / 8] & mask) != 0 ? kXrgbLit : kXrgbDark;
        }
        case PixelFormat::RGB565:
            return canvas::rgb565_to_xrgb8888(canvas::Rgb565Traits::load(row, x));
        case PixelFormat::XRGB8888:
            return canvas::Xrgb8888Traits::load(row, x);
    }
    return kXrgbDark;
}

bool WaylandWindowHost::poll_one(Event& out) noexcept {
    // dispatch_pending alone never touches the socket: an application that
    // stops drawing and only polls would never hear input, close or ping
    // again. The zero timeout reads whatever has arrived and hands it over
    // without waiting, so poll keeps its non-blocking word.
    constexpr timespec nowait{.tv_sec = 0, .tv_nsec = 0};
    wl_display_dispatch_timeout(display_, &nowait);
    return feed_.pop(out);
}

void WaylandWindowHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<WaylandWindowHost*>(context)->handle_flush(region);
}

bool WaylandWindowHost::poll_event_thunk(void* context, Event& out) noexcept {
    return static_cast<WaylandWindowHost*>(context)->poll_one(out);
}

std::uint64_t WaylandWindowHost::clock_thunk(void* /*context*/) noexcept {
    // Milliseconds on a steady clock since the first call: the epoch is the
    // host's own business, durations read by unsigned subtraction.
    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - epoch).count());
}

} // namespace visor::host::wayland

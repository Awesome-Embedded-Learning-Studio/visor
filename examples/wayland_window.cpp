// A monochrome canvas in a Wayland window: the same eyeball lane the X11
// example draws, on the second real host. The window opens on the display
// named by WAYLAND_DISPLAY (wl_display_connect's own default), paints a
// checkered field with a lit border, and answers pointer and key words until
// the compositor says close.
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/wayland/wayland_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/draw/pen.hpp"

#include <chrono>
#include <optional>
#include <print>
#include <thread>
#include <variant>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
constexpr SizeI kScreen{.w = 128, .h = 64};
using Frame = visor::canvas::Canvas<kScreen>;

/// The checker: a lit border, then eight-pixel squares alternating.
void paint(Frame& frame) {
    frame.set_clear(false);
    const visor::draw::Pen on{frame, true};
    const visor::draw::Pen off{frame, false};
    on.fill(RectI{.pos = PointI{0, 0}, .size = kScreen});
    off.fill(RectI{.pos = PointI{1, 1}, .size = SizeI{.w = kScreen.w - 2, .h = kScreen.h - 2}});
    for (int square_y = 0; square_y < 7; ++square_y) {
        for (int square_x = 0; square_x < 15; ++square_x) {
            if ((square_x + square_y) % 2 == 0) {
                continue;
            }
            off.fill(RectI{
                .pos = PointI{4 + (square_x * 8), 4 + (square_y * 8)},
                .size = SizeI{.w = 8, .h = 8},
            });
        }
    }
}

} // namespace

int main() {
    auto host = visor::host::wayland::WaylandWindowHost::open(nullptr, kScreen.w, kScreen.h, 2);
    if (!host.has_value()) {
        std::println(stderr, "no Wayland display answered");
        return 1;
    }
    const visor::host::HostTable table = host->table();

    Frame frame;
    paint(frame);
    if (!table.submit_frame(frame)) {
        std::println(stderr, "the frame was refused");
        return 1;
    }
    std::println(stderr, "window up: {} flushes, {} rejections", host->flush_count(),
                 host->rejected_count());

    // The event loop's shape: poll, answer, leave when the compositor says
    // close. A word every hundred milliseconds keeps the loop alive without
    // burning the core.
    for (;;) {
        visor::host::Event event{};
        if (table.poll(event)) {
            if (const auto* down = std::get_if<visor::host::PointerDown>(&event)) {
                std::println("pointer down {} at {},{}", down->button, down->pos.x, down->pos.y);
            } else if (const auto* key = std::get_if<visor::host::KeyDown>(&event)) {
                std::println("key down {}", key->code);
            } else if (const auto* wheel = std::get_if<visor::host::PointerWheel>(&event)) {
                std::println("wheel dx={} dy={}", wheel->dx, wheel->dy);
            }
        }
        if (host->close_requested()) {
            std::println("close arrived");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        (void)frame; // repainting on demand is the desktop's layer, not this lane
    }
}

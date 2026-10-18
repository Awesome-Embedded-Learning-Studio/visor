#include <algorithm>
#include <array>
#include <cstddef>
#include <print>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/pen.hpp"

namespace {

// The five areas of the gate scene, exactly as the plan spells them: one plain,
// one overlapping the first, one clipped at the bottom-right, one clipped at the
// negative origin, one fully outside the canvas.
constexpr std::array<visor::base::RectI, 5> kScene{
    {
        {.pos = visor::base::PointI{8, 4}, .size = visor::base::SizeI{.w = 40, .h = 20}},
        {.pos = visor::base::PointI{30, 10}, .size = visor::base::SizeI{.w = 60, .h = 30}},
        {.pos = visor::base::PointI{100, 50}, .size = visor::base::SizeI{.w = 60, .h = 30}},
        {.pos = visor::base::PointI{-10, -10}, .size = visor::base::SizeI{.w = 30, .h = 30}},
        {.pos = visor::base::PointI{130, 10}, .size = visor::base::SizeI{.w = 5, .h = 5}},
    },
};

/// The pure coordinate reference: no drawing involved, just containment checks.
/// Clipped areas need no special casing, a point outside the canvas is outside
/// every area by construction.
constexpr bool expected_on(int x, int y) {
    const visor::base::PointI p{x, y};
    return std::ranges::any_of(kScene, [&](const auto& area) { return area.contains(p); });
}

} // namespace

void run_diff_tests() {
    using visor::base::PointI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::ColorRole;
    using visor::draw::Pen;
    using visor::host::OffscreenHost;

    using GateCanvas = Canvas<SizeI{.w = 128, .h = 64}>;

    // Paint the scene with one pen, five fills.
    GateCanvas canvas;
    const Pen pen{canvas, true};
    for (const auto& area : kScene) {
        pen.fill(area);
    }

    // Hand the finished frame to the offscreen host, the second reader in the ring.
    OffscreenHost mirror(128, 64);
    VISOR_CHECK(mirror.table().submit_frame(canvas));

    // Three comparisons over all 8192 pixels: canvas against the reference, the
    // host's received frame against the reference, and the host against the canvas.
    // The first mismatch of each ring gets a line with its coordinates.
    std::size_t canvas_vs_reference = 0;
    std::size_t host_vs_reference = 0;
    std::size_t host_vs_canvas = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) {
            const bool from_canvas = canvas.get(PointI{x, y});
            const bool from_host = mirror.get(PointI{x, y});
            const bool reference = expected_on(x, y);
            if (from_canvas != reference) {
                if (canvas_vs_reference == 0) {
                    std::println("canvas first mismatch at ({}, {})", x, y);
                }
                ++canvas_vs_reference;
            }
            if (from_host != reference) {
                if (host_vs_reference == 0) {
                    std::println("host first mismatch at ({}, {})", x, y);
                }
                ++host_vs_reference;
            }
            if (from_host != from_canvas) {
                if (host_vs_canvas == 0) {
                    std::println("host vs canvas first mismatch at ({}, {})", x, y);
                }
                ++host_vs_canvas;
            }
        }
    }
    VISOR_CHECK_EQ(canvas_vs_reference, std::size_t{0});
    VISOR_CHECK_EQ(host_vs_reference, std::size_t{0});
    VISOR_CHECK_EQ(host_vs_canvas, std::size_t{0});

    // The graduation photo: the first image this project produces.
    VISOR_CHECK(mirror.save_pbm("visor_first_frame.pbm"));
}

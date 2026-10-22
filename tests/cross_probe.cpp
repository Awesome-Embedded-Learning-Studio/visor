// A cross-compilation probe: the layers a Cinux build links, exercised for
// a target compiler. This file is deliberately outside the desktop test
// binary -- the cross judgment hands it to the cross toolchain, so the
// whitelist layers prove they compile somewhere other than the build host.

// The include sweep below is the probe's purpose: every layer a Cinux build
// links, handed to the target compiler. Not every symbol is touched by name
// below -- the layers compile for the target by being included, which is the
// judgment -- so the one-header-one-use contract yields here.
// NOLINTBEGIN(misc-include-cleaner)
#include "visor/base/bit_ops/bit_ops.hpp"
#include "visor/base/bit_ops/bitmap.hpp"
#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/containers/ring_queue.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/point_helper.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/base/identity/id_session.hpp"
#include "visor/base/text/utf8.hpp"
#include "visor/bindings/cinux/event_wire.hpp"
#include "visor/bindings/cinux/frame_expand.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/evdev/event_wire.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/bindings/host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_convert.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/draw/pen.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/popup_layer.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/box/box.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/dropdown/dropdown.hpp"
#include "visor/ui/widgets/knob/knob.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/radio/radio.hpp"
#include "visor/ui/widgets/slider/slider.hpp"
#include "visor/ui/widgets/textbox/textbox.hpp"
#include "visor/ui/widgets/widget.hpp"
// NOLINTEND(misc-include-cleaner)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

/// Touches every linked layer with real instantiations, so the target
/// compiler sees the templates the desktop build sees, not just headers.
template <visor::base::SizeI Screen, typename Format>
bool exercise_canvas_family() {
    visor::canvas::Canvas<Screen, Format> full;
    visor::canvas::SwapCanvas<Screen, Format> swap;
    full.set_clear(true);
    swap.set_clear(false);
    const bool wrote =
        swap.set(visor::base::PointI{1, 1}, Format::from_ink(visor::canvas::Ink::white()));
    swap.swap();
    const bool lit_corner = swap.get(visor::base::PointI{1, 1});
    return wrote && lit_corner && full.pixels().size() == swap.pixels().size() &&
           swap.front_pixels().size() == full.kBufferBytes;
}

/// The probe's answer: true when the exercised layers behave on the target.
/// Anonymous on purpose: nothing on the desktop links it -- the cross
/// compiler reads this file as a standalone unit, and the maybe-unused keeps
/// that pass's unused-function warning quiet.
[[maybe_unused]] bool cross_probe_runs() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;

    bool ok = exercise_canvas_family<SizeI{.w = 16, .h = 8}, visor::canvas::Mono1Traits>();
    ok = exercise_canvas_family<SizeI{.w = 16, .h = 8}, visor::canvas::Rgb565Traits>() && ok;
    ok = exercise_canvas_family<SizeI{.w = 16, .h = 8}, visor::canvas::Xrgb8888Traits>() && ok;

    // The geometry and bit vocabulary.
    const RectI a{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}};
    const RectI b{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}};
    ok = visor::base::intersect(a, b).size == SizeI{.w = 2, .h = 2} && ok;
    ok = visor::base::bounding_box(a, b).size == SizeI{.w = 6, .h = 6} && ok;
    ok = visor::base::bit::mask_bit<std::uint8_t>(0).raw == 0x01 && ok;

    // The container and text vocabulary.
    visor::base::container::RingQueue<int, 4> queue;
    ok = queue.push(1) && queue.push(2) && ok;
    int popped = 0;
    ok = queue.pop(popped) && popped == 1 && ok;
    std::size_t cursor = 0;
    ok = visor::base::text::decode_utf8_next(std::string_view{"a"}, cursor).value_or(0) == 0x61 &&
         ok;
    ok = visor::base::bit::mask_bit<std::uint8_t>(0).raw == 0x01 && ok;

    // The translation layers, both directions of the wire.
    std::array<std::uint32_t, 4> frame{};
    const visor::host::cinux::FrameTarget target{
        .pixels = frame.data(),
        .width = 4,
        .height = 1,
        .pitch_px = 4,
    };
    const std::array<std::uint8_t, 2> mono{0xf0, 0x00};
    visor::host::cinux::expand_region(target,
                                      RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 1}},
                                      mono.data(), 1, 0xffffffffU, 0xff000000U);
    ok = frame[0] == 0xffffffffU && ok;
    // Little endian, low byte first: white leads so frame[0] answers the
    // assertion below, black follows at frame[1].
    const std::array<std::uint8_t, 8> color{0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    visor::host::cinux::expand_region_rgb565(
        target, RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 1}}, color.data(), 8);
    ok = frame[0] == 0xffffffffU && ok;

    visor::host::cinux::WireEvent wire{};
    wire.type = visor::host::cinux::WireType::MouseUp;
    const auto translated = visor::host::cinux::translate_event(wire);
    ok = translated.has_value() && ok;

    // The theme the widget tree paints through; the tree's own headers are
    // included above, so the target compiler reads every linked layer.
    const visor::ui::Theme scheme = visor::ui::kColorTheme;
    ok = scheme.accent == visor::canvas::Ink::from_rgb(0x004a62c8) && ok;

    return ok;
}

} // namespace

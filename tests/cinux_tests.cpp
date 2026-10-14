#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/cinux/event_wire.hpp"
#include "visor/bindings/cinux/frame_expand.hpp"
#include "visor/bindings/events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

void run_cinux_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;

    // The frame expansion: a mono region becomes XRGB8888 words, bit for bit,
    // with the two colors the adapter chose. Odd region x and odd width ride
    // the same bit-position formula the canvas packs with.
    {
        // An 11-wide mono strip, the chapter-four chevron bytes: MSB-first,
        // tail padding zero. Expanded into a 16-wide frame at x = 2.
        const std::array<std::uint8_t, 10> chevron{
            0xf8, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0xe0, 0x00, 0x00,
        };
        std::vector<std::uint32_t> frame(static_cast<std::size_t>(16) * 5,
                                         visor::host::cinux::kXrgbWhite);
        const visor::host::cinux::FrameTarget target{
            .pixels = frame.data(),
            .width = 16,
            .height = 5,
            .pitch_px = 16,
        };
        const RectI region{.pos = PointI{2, 0}, .size = SizeI{.w = 11, .h = 5}};
        visor::host::cinux::expand_region(target, region, chevron.data(), 2,
                                          visor::host::cinux::kXrgbBlack,
                                          visor::host::cinux::kXrgbWhite);
        const auto black = visor::host::cinux::kXrgbBlack;
        const auto white = visor::host::cinux::kXrgbWhite;
        // Row 0 is 0xf8: region x 0..4 lit -> frame x 2..6 black.
        VISOR_CHECK_EQ(frame[(0 * 16) + 2], black);
        VISOR_CHECK_EQ(frame[(0 * 16) + 6], black);
        VISOR_CHECK_EQ(frame[(0 * 16) + 7], white);  // region x 5 dark
        VISOR_CHECK_EQ(frame[(0 * 16) + 1], white);  // left of the region
        VISOR_CHECK_EQ(frame[(0 * 16) + 13], white); // past the region's odd tail
        // Row 3 is 0x01,0xe0: region x 7 lit and x 8..10 lit, frame x 9..13.
        VISOR_CHECK_EQ(frame[(3 * 16) + 9], black);
        VISOR_CHECK_EQ(frame[(3 * 16) + 12], black); // region x 10, the last lit
        VISOR_CHECK_EQ(frame[(3 * 16) + 8], white);
        VISOR_CHECK_EQ(frame[(3 * 16) + 13], white); // region x 11 is padding
        VISOR_CHECK_EQ(frame[(3 * 16) + 14], white); // outside the region
        // Untouched rows outside the region stay the frame's own.
        VISOR_CHECK_EQ(frame[(4 * 16) + 5], white);
    }

    // The expansion clips to the frame and honors the frame's own pitch: a
    // region hanging off every edge lands only where the frame has pixels.
    {
        const std::array<std::uint8_t, 6> all_lit{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        std::vector<std::uint32_t> frame(static_cast<std::size_t>(20) * 4, 0);
        const visor::host::cinux::FrameTarget target{
            .pixels = frame.data(),
            .width = 20,
            .height = 4,
            .pitch_px = 25,
        };
        const RectI overhang{.pos = PointI{-4, -1}, .size = SizeI{.w = 12, .h = 3}};
        visor::host::cinux::expand_region(target, overhang, all_lit.data(), 2, 7, 0);
        // Clipped to frame x 0..7, y 0..1: eight words per row lit, the
        // frame's padding columns past width untouched.
        for (int x = 0; x < 8; ++x) {
            VISOR_CHECK_EQ(frame[(0 * 25) + x], 7U);
            VISOR_CHECK_EQ(frame[(1 * 25) + x], 7U);
        }
        VISOR_CHECK_EQ(frame[(0 * 25) + 8], 0U);
        VISOR_CHECK_EQ(frame[(2 * 25) + 0], 0U);
        VISOR_CHECK_EQ(frame[(0 * 25) + 20], 0U); // the pitch's slack column
    }

    // The scancode table: the named keys arrive by their raw codes, and the
    // arrows land on the core's out-of-band values, not their HID ids.
    {
        VISOR_CHECK_EQ(visor::host::cinux::named_key_for(0x0e).value_or(0),
                       visor::host::kKeyBackspace);
        VISOR_CHECK_EQ(visor::host::cinux::named_key_for(0x1c).value_or(0), visor::host::kKeyEnter);
        VISOR_CHECK_EQ(visor::host::cinux::named_key_for(0x52).value_or(0), visor::host::kKeyUp);
        VISOR_CHECK_EQ(visor::host::cinux::named_key_for(0x50).value_or(0), visor::host::kKeyLeft);
        const std::uint8_t unknown_scancode = 0x7b;
        VISOR_CHECK(!visor::host::cinux::named_key_for(unknown_scancode).has_value());
    }

    // The event translation, word by word: mouse words carry absolute
    // positions with button 1, key words ride ASCII first then the table,
    // and nothing mappable translates to nothing.
    {
        visor::host::cinux::WireEvent wire{};
        wire.type = visor::host::cinux::WireType::MouseDown;
        wire.mouse = {
            .x = 12,
            .y = 34,
            .dx = 1,
            .dy = 2,
            .buttons = 1,
            .left = true,
            .right = false,
            .middle = false,
        };
        const auto down = visor::host::cinux::translate_event(wire);
        VISOR_CHECK(down.has_value());
        if (down.has_value()) {
            const auto* pointer = std::get_if<visor::host::PointerDown>(&*down);
            VISOR_CHECK(pointer != nullptr);
            VISOR_CHECK_EQ(pointer->pos, (PointI{12, 34}));
            VISOR_CHECK_EQ(pointer->button, 1U);
        }
        wire.type = visor::host::cinux::WireType::Move;
        wire.mouse.x = 40;
        wire.mouse.y = 41;
        const auto move = visor::host::cinux::translate_event(wire);
        VISOR_CHECK(move.has_value());
        if (move.has_value()) {
            const auto* pointer = std::get_if<visor::host::PointerMove>(&*move);
            VISOR_CHECK(pointer != nullptr);
            VISOR_CHECK_EQ(pointer->pos, (PointI{40, 41}));
        }
        // A printable key rides its ASCII byte.
        wire.type = visor::host::cinux::WireType::KeyDown;
        wire.key = {
            .ascii = 'h',
            .scancode = 0x23,
            .pressed = true,
            .shift = false,
            .ctrl = false,
            .alt = false,
        };
        const auto letter = visor::host::cinux::translate_event(wire);
        VISOR_CHECK(letter.has_value());
        if (letter.has_value()) {
            const auto* key = std::get_if<visor::host::KeyDown>(&*letter);
            VISOR_CHECK(key != nullptr);
            VISOR_CHECK_EQ(key->code, static_cast<std::uint32_t>('h'));
        }
        // A scancode-only arrow lands on the core's named, out-of-band value.
        wire.key = {
            .ascii = 0,
            .scancode = 0x52,
            .pressed = true,
            .shift = false,
            .ctrl = false,
            .alt = false,
        };
        const auto up = visor::host::cinux::translate_event(wire);
        VISOR_CHECK(up.has_value());
        if (up.has_value()) {
            const auto* key = std::get_if<visor::host::KeyDown>(&*up);
            VISOR_CHECK(key != nullptr);
            VISOR_CHECK_EQ(key->code, visor::host::kKeyUp);
        }
        // The release of the same arrow is a KeyUp with the same code.
        wire.key.pressed = false;
        wire.type = visor::host::cinux::WireType::KeyUp;
        const auto release = visor::host::cinux::translate_event(wire);
        VISOR_CHECK(release.has_value());
        if (release.has_value()) {
            const auto* key = std::get_if<visor::host::KeyUp>(&*release);
            VISOR_CHECK(key != nullptr);
            VISOR_CHECK_EQ(key->code, visor::host::kKeyUp);
        }
        // A key the core cannot name translates to nothing.
        wire.key = {
            .ascii = 0,
            .scancode = 0x7b,
            .pressed = true,
            .shift = false,
            .ctrl = false,
            .alt = false,
        };
        VISOR_CHECK(!visor::host::cinux::translate_event(wire).has_value());
        // So does a type out of the wire's range.
        wire.type = static_cast<visor::host::cinux::WireType>(9);
        VISOR_CHECK(!visor::host::cinux::translate_event(wire).has_value());
    }
}

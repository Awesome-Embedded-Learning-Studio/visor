#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/dropdown/dropdown.hpp"
#include "visor/ui/widgets/knob/knob.hpp"
#include "visor/ui/widgets/label/label.hpp"
#include "visor/ui/widgets/radio/radio.hpp"
#include "visor/ui/widgets/slider/slider.hpp"
#include "visor/ui/widgets/textbox/textbox.hpp"
#include "visor/ui/widgets/widget.hpp"
#include <array>

#include <cstddef>
#include <cstdint>

void run_theme_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::ColorRole;

    // The rounded fill, radius 2 on a 7x5: the zone table says row 0 insets
    // two from each side, rows 1 and 3 inset one, the middle row is whole.
    {
        Canvas<SizeI{.w = 9, .h = 7}> canvas;
        canvas.set_clear(false);
        const visor::ui::CanvasPainter painter{canvas};
        const visor::ui::CornerRadius all{
            .top_left = 2,
            .top_right = 2,
            .bottom_right = 2,
            .bottom_left = 2,
        };
        painter.fill_rounded(RectI{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}},
                             ColorRole::Foreground, all);
        // Row 0 (y=1): span x 3..5 -- the corners' whole zone rows are dark.
        VISOR_CHECK(canvas.get(PointI{3, 1}) == true);
        VISOR_CHECK(canvas.get(PointI{5, 1}) == true);
        VISOR_CHECK(canvas.get(PointI{2, 1}) == false);
        VISOR_CHECK(canvas.get(PointI{6, 1}) == false);
        // Row 1 (y=2): inset one each side, the zone's (1,1) pixels survive.
        VISOR_CHECK(canvas.get(PointI{2, 2}) == true);
        VISOR_CHECK(canvas.get(PointI{6, 2}) == true);
        VISOR_CHECK(canvas.get(PointI{1, 2}) == false);
        VISOR_CHECK(canvas.get(PointI{7, 2}) == false);
        // The middle row (y=3) is whole.
        VISOR_CHECK(canvas.get(PointI{1, 3}) == true);
        VISOR_CHECK(canvas.get(PointI{7, 3}) == true);
        // Rows 4 and 5 (y=5, y=6) mirror rows 1 and 0.
        VISOR_CHECK(canvas.get(PointI{2, 4}) == true);
        VISOR_CHECK(canvas.get(PointI{2, 5}) == false);
        VISOR_CHECK(canvas.get(PointI{3, 5}) == true);
    }

    // Per-corner: only the top-left rounds, the other three corners stay
    // square -- the inset table applies where a radius lives and nowhere
    // else.
    {
        Canvas<SizeI{.w = 9, .h = 7}> canvas;
        canvas.set_clear(false);
        const visor::ui::CanvasPainter painter{canvas};
        const visor::ui::CornerRadius one{
            .top_left = 3,
            .top_right = 0,
            .bottom_right = 0,
            .bottom_left = 0,
        };
        painter.fill_rounded(RectI{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}},
                             ColorRole::Foreground, one);
        // The 5-tall rect clamps the radius to 2, so the zone table is the
        // radius-2 one: row 0 insets two, row 1 insets one.
        VISOR_CHECK(canvas.get(PointI{4, 1}) == true);
        VISOR_CHECK(canvas.get(PointI{3, 1}) == true); // after a 2-inset
        VISOR_CHECK(canvas.get(PointI{2, 1}) == false);
        VISOR_CHECK(canvas.get(PointI{2, 2}) == true); // after a 1-inset
        VISOR_CHECK(canvas.get(PointI{1, 2}) == false);
        // The other three corners are square.
        VISOR_CHECK(canvas.get(PointI{7, 1}) == true);
        VISOR_CHECK(canvas.get(PointI{1, 5}) == true);
        VISOR_CHECK(canvas.get(PointI{7, 5}) == true);
    }

    // Radius zero is the plain fill, pixel for pixel; an oversized radius
    // clamps to half the shorter side, so 9 and 2 agree on a 7x5.
    {
        Canvas<SizeI{.w = 9, .h = 7}> square;
        Canvas<SizeI{.w = 9, .h = 7}> rounded;
        const visor::ui::CanvasPainter square_painter{square};
        const visor::ui::CanvasPainter rounded_painter{rounded};
        const RectI area{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}};
        square_painter.fill_rounded(area, ColorRole::Foreground, {});
        rounded_painter.fill(area, ColorRole::Foreground);
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 9; ++x) {
                VISOR_CHECK(square.get(PointI{x, y}) == rounded.get(PointI{x, y}));
            }
        }
        const visor::ui::CornerRadius huge{
            .top_left = 9,
            .top_right = 9,
            .bottom_right = 9,
            .bottom_left = 9,
        };
        const visor::ui::CornerRadius two{
            .top_left = 2,
            .top_right = 2,
            .bottom_right = 2,
            .bottom_left = 2,
        };
        square.set_clear(false);
        rounded.set_clear(false);
        square_painter.fill_rounded(area, ColorRole::Foreground, huge);
        rounded_painter.fill_rounded(area, ColorRole::Foreground, two);
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 9; ++x) {
                VISOR_CHECK(square.get(PointI{x, y}) == rounded.get(PointI{x, y}));
            }
        }
    }

    // The theme wiring: a light-scheme tree paints the pixels every control
    // hardcoded before themes arrived, the dark scheme flips the polarity,
    // and the switch itself reports every widget's bounds.
    {
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 10}});
        visor::ui::Button button(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 6, .h = 4}});
        visor::ui::Checkbox box(RectI{.pos = PointI{10, 2}, .size = SizeI{.w = 8, .h = 8}});
        VISOR_CHECK(panel.add_child(&button));
        VISOR_CHECK(panel.add_child(&box));
        visor::ui::DirtyRegions debts;
        panel.set_dirty_sink(&debts);

        using Frame = Canvas<SizeI{.w = 20, .h = 10}>;
        Frame unwired;
        unwired.set_clear(false);
        panel.draw(visor::ui::CanvasPainter{unwired});
        VISOR_CHECK_EQ(unwired.get(PointI{3, 3}), true);  // the button's light ink
        VISOR_CHECK_EQ(unwired.get(PointI{13, 4}), true); // the checkbox frame

        // Wiring the light scheme: pixels identical to the hardcoded
        // polarity, and the walk reports every widget (the pointer changed
        // from null, the report fires regardless of polarity).
        debts.clear();
        panel.set_theme(&visor::ui::kLightTheme);
        // All three bounds nest in the panel's, so the reports merge into
        // one region covering it.
        VISOR_CHECK_EQ(debts.count(), std::size_t{1});
        Frame light;
        light.set_clear(false);
        panel.draw(visor::ui::CanvasPainter{light, *panel.theme()});
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 20; ++x) {
                VISOR_CHECK(light.get(PointI{x, y}) == unwired.get(PointI{x, y}));
            }
        }

        // The switch to dark flips both controls and the frame's carve.
        debts.clear();
        panel.set_theme(&visor::ui::kDarkTheme);
        VISOR_CHECK_EQ(debts.count(), std::size_t{1}); // merged the same way
        Frame dark;
        dark.set_clear(false);
        panel.draw(visor::ui::CanvasPainter{dark, *panel.theme()});
        VISOR_CHECK_EQ(dark.get(PointI{3, 3}), false);  // the button's dark ink
        VISOR_CHECK_EQ(dark.get(PointI{13, 4}), false); // the frame flips with the theme
        // A late child inherits the scheme of the parent it joins.
        visor::ui::Button late(RectI{.pos = PointI{2, 6}, .size = SizeI{.w = 6, .h = 3}});
        VISOR_CHECK(panel.add_child(&late));
        dark.set_clear(false);
        panel.draw(visor::ui::CanvasPainter{dark, *panel.theme()});
        VISOR_CHECK_EQ(dark.get(PointI{3, 7}), false); // dark ink without its own wiring
        // Same pointer again wires nothing twice.
        debts.clear();
        panel.set_theme(&visor::ui::kDarkTheme);
        VISOR_CHECK_EQ(debts.count(), std::size_t{0});
    }

    // The corner radius rides the scheme: a themed button rounds its fill,
    // its corners stay the panel's ink.
    {
        const visor::ui::Theme rounded_light{
            .background = visor::canvas::Ink::black(),
            .foreground = visor::canvas::Ink::white(),
            .corner_radius = {.top_left = 2, .top_right = 2, .bottom_right = 2, .bottom_left = 2},
        };
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 10}});
        panel.fill = ColorRole::Background; // dark ground, so the corner cut reads
        visor::ui::Button button(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 7, .h = 5}});
        VISOR_CHECK(panel.add_child(&button));
        panel.set_theme(&rounded_light);
        Canvas<SizeI{.w = 20, .h = 10}> canvas;
        canvas.set_clear(false);
        panel.draw(visor::ui::CanvasPainter{canvas, rounded_light});
        VISOR_CHECK(canvas.get(PointI{2, 2}) == false); // the rounded-off corner
        VISOR_CHECK(canvas.get(PointI{4, 2}) == true);  // the row's span
        VISOR_CHECK(canvas.get(PointI{2, 3}) == false); // the side inset row (v=1)
        VISOR_CHECK(canvas.get(PointI{3, 3}) == true);  // the body
    }
    // Every control honors scheme polarity, stays within its dirty bounds,
    // and produces identical pixels when replayed through five-row strips.
    {
        constexpr std::array<std::uint8_t, 4> bits{0xc0, 0xc0, 0xc0, 0xc0};
        constexpr std::array<visor::draw::GlyphEntry, 1> glyphs{
            {{.codepoint = 'a', .width = 2, .height = 4, .byte_offset = 0, .advance = 3}},
        };
        const visor::draw::MonoFont font{.line_height = 4, .glyphs = glyphs, .bits = bits};
        visor::ui::Rectangle panel(RectI{.pos = {0, 0}, .size = {.w = 64, .h = 64}});
        visor::ui::Button button(RectI{.pos = {2, 2}, .size = {.w = 18, .h = 12}});
        button.set_label("aaaaaaaaaa", font);
        visor::ui::Checkbox checkbox(RectI{.pos = {22, 2}, .size = {.w = 14, .h = 14}});
        visor::ui::Slider slider(RectI{.pos = {40, 2}, .size = {.w = 20, .h = 12}});
        slider.set_value(6);
        visor::ui::TextBox textbox(RectI{.pos = {2, 20}, .size = {.w = 22, .h = 12}}, font,
                                   ColorRole::Foreground);
        textbox.set_text("aaaaaaaaaaaaaaaa");
        textbox.set_focused(true);
        visor::ui::RadioGroup radio(RectI{.pos = {28, 20}, .size = {.w = 32, .h = 20}}, font,
                                    ColorRole::Foreground);
        VISOR_CHECK(radio.add_option("aaaaaaaaaaaaaaaaaaaa"));
        VISOR_CHECK(radio.add_option("a"));
        radio.select(1);
        visor::ui::Dropdown dropdown(RectI{.pos = {2, 36}, .size = {.w = 24, .h = 12}}, font,
                                     ColorRole::Foreground);
        VISOR_CHECK(dropdown.add_option("aaaaaaaaaaaaaaaaaaaa"));
        VISOR_CHECK(dropdown.add_option("a"));
        dropdown.select(0);
        visor::ui::Knob knob(RectI{.pos = {40, 42}, .size = {.w = 20, .h = 20}}, 0, 100, 50);
        visor::ui::Label label(PointI{32, 52}, "aa", font, ColorRole::Foreground);
        const std::array<visor::ui::Widget*, 8> controls{
            &button, &checkbox, &slider, &textbox, &radio, &dropdown, &knob, &label,
        };
        for (auto* control : controls) {
            VISOR_CHECK(panel.add_child(control));
        }
        using Frame = Canvas<SizeI{.w = 64, .h = 64}>;
        for (int state = 0; state < 3; ++state) {
            if (state == 1) {
                button.on_pointer_down({3, 3}, {});
                checkbox.on_key_down(visor::host::kKeyEnter, {});
                dropdown.set_open(true);
            } else if (state == 2) {
                button.on_pointer_up({3, 3});
                button.set_outlined(true);
                dropdown.on_key_down(visor::host::kKeyDown, {});
            }
            Frame light;
            Frame dark;
            const auto render = [&](const visor::ui::Theme& scheme, Frame& full) {
                panel.fill = ColorRole::Background;
                panel.set_theme(&scheme);
                panel.draw(visor::ui::CanvasPainter{full, scheme});
                for (int top = 0; top < 64; top += 5) {
                    Canvas<SizeI{.w = 64, .h = 5}> strip;
                    panel.draw(visor::ui::CanvasPainter{
                        strip, RectI{.pos = {0, top}, .size = {.w = 64, .h = 5}}, scheme});
                    for (int y = 0; y < 5 && top + y < 64; ++y) {
                        for (int x = 0; x < 64; ++x) {
                            VISOR_CHECK_EQ(strip.get(PointI{x, y}), full.get(PointI{x, top + y}));
                        }
                    }
                }
                for (const auto* control : controls) {
                    Frame isolated;
                    isolated.set_clear(visor::canvas::Mono1Traits::from_ink(scheme.background));
                    control->draw(visor::ui::CanvasPainter{isolated, scheme});
                    for (int y = 0; y < 64; ++y) {
                        for (int x = 0; x < 64; ++x) {
                            if (!control->bounds().contains(PointI{x, y})) {
                                VISOR_CHECK_EQ(
                                    isolated.get(PointI{x, y}),
                                    visor::canvas::Mono1Traits::from_ink(scheme.background));
                            }
                        }
                    }
                }
            };
            render(visor::ui::kLightTheme, light);
            render(visor::ui::kDarkTheme, dark);
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    VISOR_CHECK(light.get(PointI{x, y}) != dark.get(PointI{x, y}));
                }
            }
        }
    }
    // Clipping must handle scaled overhangs on all four edges without
    // changing the glyph's placement or writing into neighboring controls.
    {
        constexpr std::array<std::uint8_t, 4> bits{0xe0, 0xa0, 0xe0, 0xe0};
        constexpr std::array<visor::draw::GlyphEntry, 1> glyphs{
            {
                {
                    .codepoint = 'a',
                    .width = 3,
                    .height = 4,
                    .byte_offset = 0,
                    .advance = 4,
                    .offset_x = -2,
                    .offset_y = -1,
                },
            },
        };
        const visor::draw::MonoFont font{.line_height = 4, .glyphs = glyphs, .bits = bits};
        Canvas<SizeI{.w = 24, .h = 24}> full;
        Canvas<SizeI{.w = 24, .h = 24}> clipped;
        const RectI clip{.pos = {5, 5}, .size = {.w = 7, .h = 7}};
        visor::ui::draw_text(visor::ui::CanvasPainter{full}, {6, 6}, font, "aa\naa",
                             ColorRole::Foreground, 2);
        visor::ui::draw_text_clipped(visor::ui::CanvasPainter{clipped}, clip, {6, 6}, font,
                                     "aa\naa", ColorRole::Foreground, 2);
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 24; ++x) {
                const auto expected = clip.contains(PointI{x, y}) ? full.get(PointI{x, y}) : false;
                VISOR_CHECK_EQ(clipped.get(PointI{x, y}), expected);
            }
        }
    }
}

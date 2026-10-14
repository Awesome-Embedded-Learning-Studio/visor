#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"

#include <cstddef>

void run_theme_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::Color;

    // The rounded fill, radius 2 on a 7x5: the zone table says row 0 insets
    // two from each side, rows 1 and 3 inset one, the middle row is whole.
    {
        Canvas<SizeI{.w = 9, .h = 7}> canvas;
        canvas.set_clear(Color::Off);
        const visor::ui::CanvasPainter painter{canvas};
        const visor::ui::CornerRadius all{
            .top_left = 2,
            .top_right = 2,
            .bottom_right = 2,
            .bottom_left = 2,
        };
        painter.fill_rounded(RectI{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}}, Color::On,
                             all);
        // Row 0 (y=1): span x 3..5 -- the corners' whole zone rows are dark.
        VISOR_CHECK(canvas.get(PointI{3, 1}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{5, 1}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{2, 1}) == Color::Off);
        VISOR_CHECK(canvas.get(PointI{6, 1}) == Color::Off);
        // Row 1 (y=2): inset one each side, the zone's (1,1) pixels survive.
        VISOR_CHECK(canvas.get(PointI{2, 2}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{6, 2}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{1, 2}) == Color::Off);
        VISOR_CHECK(canvas.get(PointI{7, 2}) == Color::Off);
        // The middle row (y=3) is whole.
        VISOR_CHECK(canvas.get(PointI{1, 3}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{7, 3}) == Color::On);
        // Rows 4 and 5 (y=5, y=6) mirror rows 1 and 0.
        VISOR_CHECK(canvas.get(PointI{2, 4}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{2, 5}) == Color::Off);
        VISOR_CHECK(canvas.get(PointI{3, 5}) == Color::On);
    }

    // Per-corner: only the top-left rounds, the other three corners stay
    // square -- the inset table applies where a radius lives and nowhere
    // else.
    {
        Canvas<SizeI{.w = 9, .h = 7}> canvas;
        canvas.set_clear(Color::Off);
        const visor::ui::CanvasPainter painter{canvas};
        const visor::ui::CornerRadius one{
            .top_left = 3,
            .top_right = 0,
            .bottom_right = 0,
            .bottom_left = 0,
        };
        painter.fill_rounded(RectI{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}}, Color::On,
                             one);
        // The 5-tall rect clamps the radius to 2, so the zone table is the
        // radius-2 one: row 0 insets two, row 1 insets one.
        VISOR_CHECK(canvas.get(PointI{4, 1}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{3, 1}) == Color::On); // after a 2-inset
        VISOR_CHECK(canvas.get(PointI{2, 1}) == Color::Off);
        VISOR_CHECK(canvas.get(PointI{2, 2}) == Color::On); // after a 1-inset
        VISOR_CHECK(canvas.get(PointI{1, 2}) == Color::Off);
        // The other three corners are square.
        VISOR_CHECK(canvas.get(PointI{7, 1}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{1, 5}) == Color::On);
        VISOR_CHECK(canvas.get(PointI{7, 5}) == Color::On);
    }

    // Radius zero is the plain fill, pixel for pixel; an oversized radius
    // clamps to half the shorter side, so 9 and 2 agree on a 7x5.
    {
        Canvas<SizeI{.w = 9, .h = 7}> square;
        Canvas<SizeI{.w = 9, .h = 7}> rounded;
        const visor::ui::CanvasPainter square_painter{square};
        const visor::ui::CanvasPainter rounded_painter{rounded};
        const RectI area{.pos = PointI{1, 1}, .size = SizeI{.w = 7, .h = 5}};
        square_painter.fill_rounded(area, Color::On, {});
        rounded_painter.fill(area, Color::On);
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
        square.set_clear(Color::Off);
        rounded.set_clear(Color::Off);
        square_painter.fill_rounded(area, Color::On, huge);
        rounded_painter.fill_rounded(area, Color::On, two);
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
        unwired.set_clear(Color::Off);
        panel.draw(visor::ui::CanvasPainter{unwired});
        VISOR_CHECK_EQ(unwired.get(PointI{3, 3}), Color::On);   // the button's light ink
        VISOR_CHECK_EQ(unwired.get(PointI{12, 4}), Color::Off); // the checkbox frame

        // Wiring the light scheme: pixels identical to the hardcoded
        // polarity, and the walk reports every widget (the pointer changed
        // from null, the report fires regardless of polarity).
        debts.clear();
        panel.set_theme(&visor::ui::kLightTheme);
        // All three bounds nest in the panel's, so the reports merge into
        // one region covering it.
        VISOR_CHECK_EQ(debts.count(), std::size_t{1});
        Frame light;
        light.set_clear(Color::Off);
        panel.draw(visor::ui::CanvasPainter{light});
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
        dark.set_clear(Color::Off);
        panel.draw(visor::ui::CanvasPainter{dark});
        VISOR_CHECK_EQ(dark.get(PointI{3, 3}), Color::Off); // the button's dark ink
        VISOR_CHECK_EQ(dark.get(PointI{12, 4}), Color::On); // the frame carves as ink now
        // A late child inherits the scheme of the parent it joins.
        visor::ui::Button late(RectI{.pos = PointI{2, 6}, .size = SizeI{.w = 6, .h = 3}});
        VISOR_CHECK(panel.add_child(&late));
        dark.set_clear(Color::Off);
        panel.draw(visor::ui::CanvasPainter{dark});
        VISOR_CHECK_EQ(dark.get(PointI{3, 7}), Color::Off); // dark ink without its own wiring
        // Same pointer again wires nothing twice.
        debts.clear();
        panel.set_theme(&visor::ui::kDarkTheme);
        VISOR_CHECK_EQ(debts.count(), std::size_t{0});
    }

    // The corner radius rides the scheme: a themed button rounds its fill,
    // its corners stay the panel's ink.
    {
        const visor::ui::Theme rounded_light{
            .background = Color::Off,
            .foreground = Color::On,
            .corner_radius = {.top_left = 2, .top_right = 2, .bottom_right = 2, .bottom_left = 2},
        };
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 10}});
        panel.fill = Color::Off; // dark ground, so the corner cut reads
        visor::ui::Button button(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 7, .h = 5}});
        VISOR_CHECK(panel.add_child(&button));
        panel.set_theme(&rounded_light);
        Canvas<SizeI{.w = 20, .h = 10}> canvas;
        canvas.set_clear(Color::Off);
        panel.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(canvas.get(PointI{2, 2}) == Color::Off); // the rounded-off corner
        VISOR_CHECK(canvas.get(PointI{4, 2}) == Color::On);  // the row's span
        VISOR_CHECK(canvas.get(PointI{2, 3}) == Color::Off); // the side inset row (v=1)
        VISOR_CHECK(canvas.get(PointI{3, 3}) == Color::On);  // the body
    }
}

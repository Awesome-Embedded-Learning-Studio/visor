#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_convert.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/theme.hpp"

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;
using visor::canvas::Ink;
using visor::canvas::Mono1Traits;
using visor::canvas::Rgb565Traits;
using visor::canvas::Xrgb8888Traits;
using visor::host::OffscreenHost;
using visor::host::PixelFormat;

constexpr SizeI kFrame{.w = 8, .h = 2};
constexpr SizeI kPanel{.w = 16, .h = 8};
const RectI kFrameBounds{.pos = PointI{0, 0}, .size = kFrame};
const RectI kPanelBounds{.pos = PointI{0, 0}, .size = kPanel};

/// The iris blue the color preset names, in each format's own pixel.
constexpr std::uint32_t kIrisRgb = 0x004a62c8;
constexpr std::uint16_t kIrisRgb565 = 0x4b19;

/// A canvas with no format argument is still the mono canvas: the default
/// parameter is what kept every pre-color call site compiling untouched.
void the_default_format_is_mono() {
    using Mono = Canvas<kFrame>;
    static_assert(std::is_same_v<Mono::Pixel, bool>);
    static_assert(std::is_same_v<Mono::Traits, Mono1Traits>);
    static_assert(Mono::kStrideBytes == 1UZ);

    Mono frame;
    frame.set_clear(true);
    VISOR_CHECK_EQ(frame.get(PointI{0, 0}), true);
    frame.set_clear(false);
    VISOR_CHECK_EQ(frame.get(PointI{0, 0}), false);
}

/// RGB565: two bytes a pixel, low byte first, and the Ink mapping keeps five,
/// six and five bits of the three channels.
void rgb565_pixels() {
    using Color = Canvas<kFrame, Rgb565Traits>;
    static_assert(std::is_same_v<Color::Pixel, std::uint16_t>);
    static_assert(Color::kStrideBytes == 16UZ);
    static_assert(Color::kStrideBytes == Color::kWidth * 2);

    VISOR_CHECK_EQ(Rgb565Traits::from_ink(Ink::black()), std::uint16_t{0x0000});
    VISOR_CHECK_EQ(Rgb565Traits::from_ink(Ink::white()), std::uint16_t{0xffff});
    VISOR_CHECK_EQ(Rgb565Traits::from_ink(Ink::from_rgb(kIrisRgb)), kIrisRgb565);

    Color frame;
    VISOR_CHECK_EQ(frame.get(PointI{0, 0}), std::uint16_t{0});
    VISOR_CHECK(frame.set(PointI{1, 0}, kIrisRgb565));
    VISOR_CHECK_EQ(frame.get(PointI{1, 0}), kIrisRgb565);

    // The byte order on the wire is little endian: x = 1 starts at byte 2.
    VISOR_CHECK_EQ(frame.pixels()[2], std::uint8_t{0x19});
    VISOR_CHECK_EQ(frame.pixels()[3], std::uint8_t{0x4b});

    // One pixel written, its neighbours untouched.
    VISOR_CHECK_EQ(frame.get(PointI{0, 0}), std::uint16_t{0});
    VISOR_CHECK_EQ(frame.get(PointI{2, 0}), std::uint16_t{0});

    // Bounds: an out-of-range set is refused, an out-of-range get reads zero.
    VISOR_CHECK(!frame.set(PointI{8, 0}, kIrisRgb565));
    VISOR_CHECK(!frame.set(PointI{-1, 0}, kIrisRgb565));
    VISOR_CHECK_EQ(frame.get(PointI{8, 0}), std::uint16_t{0});

    // The two-state fill lands white and black in the format's own pixels.
    Color ground;
    ground.set_clear(true);
    VISOR_CHECK_EQ(ground.get(PointI{7, 1}), std::uint16_t{0xffff});
    ground.set_clear(false);
    VISOR_CHECK_EQ(ground.get(PointI{7, 1}), std::uint16_t{0});

    // A whole-surface fill writes every pixel of every row.
    Color filled;
    filled.fill(kIrisRgb565);
    for (std::size_t y = 0; y < Color::kHeight; ++y) {
        for (std::size_t x = 0; x < Color::kWidth; ++x) {
            VISOR_CHECK_EQ(filled.get(PointI{static_cast<int>(x), static_cast<int>(y)}),
                           kIrisRgb565);
        }
    }
}

/// XRGB8888: four bytes a pixel, the alpha byte forced opaque, low byte first.
void xrgb8888_pixels() {
    using Wide = Canvas<SizeI{.w = 4, .h = 1}, Xrgb8888Traits>;
    static_assert(std::is_same_v<Wide::Pixel, std::uint32_t>);
    static_assert(Wide::kStrideBytes == 16UZ);

    // Alpha is dropped on the way in: a theme value cannot make a pixel translucent.
    VISOR_CHECK_EQ(Xrgb8888Traits::from_ink(Ink{0x00112233U}), std::uint32_t{0xff112233});
    VISOR_CHECK_EQ(Xrgb8888Traits::from_ink(Ink{0x80112233U}), std::uint32_t{0xff112233});

    Wide frame;
    VISOR_CHECK(frame.set(PointI{2, 0}, Xrgb8888Traits::from_ink(Ink::from_rgb(kIrisRgb))));
    VISOR_CHECK_EQ(frame.get(PointI{2, 0}), std::uint32_t{0xff4a62c8});
    VISOR_CHECK_EQ(frame.pixels()[8], std::uint8_t{0xc8});
    VISOR_CHECK_EQ(frame.pixels()[11], std::uint8_t{0xff});
    VISOR_CHECK_EQ(frame.get(PointI{1, 0}), std::uint32_t{0});
}

/// The conversion matrix: 565 to 8888 widens by bit replication, and every one
/// of the 65536 narrow pixels survives the round trip unchanged.
void format_conversions() {
    using visor::canvas::rgb565_to_xrgb8888;
    using visor::canvas::xrgb8888_to_rgb565;

    VISOR_CHECK_EQ(rgb565_to_xrgb8888(0x0000), std::uint32_t{0xff000000});
    VISOR_CHECK_EQ(rgb565_to_xrgb8888(0xffff), std::uint32_t{0xffffffff});
    // 565 loses the low bits of each channel, so the wide pixel is the iris
    // blue rounded down: the round trip lands back on the same 565 value, not
    // on the original 24-bit one.
    VISOR_CHECK_EQ(xrgb8888_to_rgb565(rgb565_to_xrgb8888(kIrisRgb565)), kIrisRgb565);

    for (std::uint32_t value = 0; value <= 0xffffU; ++value) {
        const auto narrow = static_cast<std::uint16_t>(value);
        VISOR_CHECK_EQ(xrgb8888_to_rgb565(rgb565_to_xrgb8888(narrow)), narrow);
    }

    // The wide direction loses the low bits of each channel, the same cut the
    // traits make: two blues a bucket apart land on the same 565 pixel.
    VISOR_CHECK_EQ(xrgb8888_to_rgb565(0xff112233), xrgb8888_to_rgb565(0xff172033));
}

/// A painter over a color canvas keeps every role's own value, because the
/// format has the room for the grays mono folds away.
void painter_in_color() {
    using Panel = Canvas<kPanel, Rgb565Traits>;
    Panel panel;
    const visor::ui::CanvasPainter painter{panel, visor::ui::kColorTheme};

    painter.fill(kPanelBounds, ColorRole::Background);
    VISOR_CHECK_EQ(panel.get(PointI{0, 0}), Rgb565Traits::from_ink(Ink::black()));

    painter.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}}, ColorRole::Accent);
    VISOR_CHECK_EQ(panel.get(PointI{3, 3}), kIrisRgb565);
    VISOR_CHECK_EQ(panel.get(PointI{5, 5}), Rgb565Traits::from_ink(Ink::black()));

    // The four roles the mono presets collapse to the same value stay apart here.
    painter.fill(RectI{.pos = PointI{4, 0}, .size = SizeI{.w = 4, .h = 4}}, ColorRole::Muted);
    painter.fill(RectI{.pos = PointI{8, 0}, .size = SizeI{.w = 4, .h = 4}}, ColorRole::Border);
    painter.fill(RectI{.pos = PointI{12, 0}, .size = SizeI{.w = 4, .h = 4}}, ColorRole::Disabled);
    const std::uint16_t muted = panel.get(PointI{5, 1});
    const std::uint16_t border = panel.get(PointI{9, 1});
    const std::uint16_t disabled = panel.get(PointI{13, 1});
    VISOR_CHECK(muted != border);
    VISOR_CHECK(border != disabled);
    VISOR_CHECK(muted != disabled);
}

/// The pen verbs in color: a single plot and a rounded fill both write the
/// role's own pixel, and the corner discs still decide which pixels exist.
void strokes_in_color() {
    using Panel = Canvas<kPanel, Rgb565Traits>;
    Panel panel;
    const visor::ui::CanvasPainter painter{panel, visor::ui::kColorTheme};
    painter.fill(kPanelBounds, ColorRole::Background);

    painter.plot(PointI{1, 1}, ColorRole::Accent);
    VISOR_CHECK_EQ(panel.get(PointI{1, 1}), kIrisRgb565);
    VISOR_CHECK_EQ(panel.get(PointI{1, 2}), Rgb565Traits::from_ink(Ink::black()));

    const auto border = Rgb565Traits::from_ink(visor::ui::kColorTheme.border);
    const auto dark = Rgb565Traits::from_ink(Ink::black());
    painter.fill_rounded(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 8, .h = 8}},
                         ColorRole::Border,
                         visor::ui::CornerRadius{
                             .top_left = 2,
                             .top_right = 2,
                             .bottom_right = 2,
                             .bottom_left = 2,
                         });
    VISOR_CHECK_EQ(panel.get(PointI{4, 4}), border); // the middle of every row
    VISOR_CHECK_EQ(panel.get(PointI{0, 2}), border); // past the top corner's disc
    VISOR_CHECK_EQ(panel.get(PointI{0, 0}), dark);   // inside it, the same r x r zone
}

/// The theme switch in color: the same control, a new scheme, every pixel of the
/// accent face takes the new role value without the control changing a line.
void theme_switch_in_color() {
    using Panel = Canvas<kPanel, Rgb565Traits>;
    Panel panel;
    visor::ui::Rectangle face{kPanelBounds};
    face.fill = ColorRole::Accent;

    face.set_theme(&visor::ui::kLightTheme);
    face.draw(visor::ui::CanvasPainter{panel, visor::ui::kLightTheme});
    VISOR_CHECK_EQ(panel.get(PointI{0, 0}), std::uint16_t{0xffff});

    face.set_theme(&visor::ui::kColorTheme);
    face.draw(visor::ui::CanvasPainter{panel, visor::ui::kColorTheme});
    VISOR_CHECK_EQ(panel.get(PointI{0, 0}), kIrisRgb565);
}

/// A color theme on a mono canvas folds through the Mono1 rule: the ground is
/// pure black so the picture keeps its polarity, and every other role is lit.
/// This is why mono targets keep the mono presets.
void color_theme_on_mono() {
    Canvas<kPanel> mono;
    const visor::ui::CanvasPainter painter{mono, visor::ui::kColorTheme};
    painter.fill(kPanelBounds, ColorRole::Background);
    VISOR_CHECK_EQ(mono.get(PointI{0, 0}), false);

    painter.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 2, .h = 2}}, ColorRole::Muted);
    VISOR_CHECK_EQ(mono.get(PointI{0, 0}), true);
    VISOR_CHECK_EQ(mono.get(PointI{2, 2}), false);
}

/// A 1bpp glyph sprayed in a color role: lit source pixels become the role's
/// pixel, dark ones paint nothing.
void bitmap_tinted_by_role() {
    using Panel = Canvas<kPanel, Rgb565Traits>;
    Panel panel;
    const visor::ui::CanvasPainter painter{panel, visor::ui::kColorTheme};
    painter.fill(kPanelBounds, ColorRole::Background);

    // Two rows, two columns wide: only the top-left pixel is lit (MSB first).
    constexpr std::array<std::uint8_t, 2> rows{0x80, 0x00};
    const visor::draw::MonoBitmap glyph{.width = 2, .height = 2, .rows = rows};
    painter.draw_bitmap(PointI{0, 0}, glyph, ColorRole::Accent, 1);

    VISOR_CHECK_EQ(panel.get(PointI{0, 0}), kIrisRgb565);
    VISOR_CHECK_EQ(panel.get(PointI{1, 0}), Rgb565Traits::from_ink(Ink::black()));
    VISOR_CHECK_EQ(panel.get(PointI{0, 1}), Rgb565Traits::from_ink(Ink::black()));

    // Scale 2: the lit pixel covers a 2x2 block.
    Panel scaled;
    const visor::ui::CanvasPainter scaled_painter{scaled, visor::ui::kColorTheme};
    scaled_painter.fill(kPanelBounds, ColorRole::Background);
    scaled_painter.draw_bitmap(PointI{0, 0}, glyph, ColorRole::Accent, 2);
    VISOR_CHECK_EQ(scaled.get(PointI{1, 1}), kIrisRgb565);
    VISOR_CHECK_EQ(scaled.get(PointI{2, 0}), Rgb565Traits::from_ink(Ink::black()));
}

/// The host side: a color double receives the region in its own format, reads
/// the pixel back, and refuses a region in a format it does not speak.
void host_receives_color() {
    OffscreenHost screen{kFrame.w, kFrame.h, PixelFormat::RGB565};
    Canvas<kFrame, Rgb565Traits> frame;
    VISOR_CHECK(frame.set(PointI{1, 0}, kIrisRgb565));
    VISOR_CHECK(screen.table().submit_frame(frame));
    VISOR_CHECK_EQ(screen.flush_count(), 1);
    VISOR_CHECK_EQ(screen.get_pixel(PointI{1, 0}), std::uint32_t{kIrisRgb565});
    VISOR_CHECK_EQ(screen.get_pixel(PointI{0, 0}), std::uint32_t{0});
    VISOR_CHECK_EQ(screen.get_pixel(PointI{-1, 0}), std::uint32_t{0});
    VISOR_CHECK(!screen.get(PointI{1, 0})); // the mono read does not answer for a color host
    VISOR_CHECK(!screen.save_pbm("/dev/null"));

    // The same region to a mono host is refused and counted, not misread.
    OffscreenHost mono_screen{kFrame.w, kFrame.h};
    VISOR_CHECK(mono_screen.table().submit_frame(frame));
    VISOR_CHECK_EQ(mono_screen.rejected_count(), 1);
    VISOR_CHECK_EQ(mono_screen.flush_count(), 0);
}

/// The render seam threads the canvas's wire format through to the host, so a
/// color buffer lands in a color host without anyone naming the format twice.
void render_window_submits_the_canvas_format() {
    Canvas<kFrame, Rgb565Traits> buffer;
    OffscreenHost display{kFrame.w, kFrame.h, PixelFormat::RGB565};

    visor::ui::Rectangle area{kFrameBounds};
    area.fill = ColorRole::Accent;
    area.set_theme(&visor::ui::kColorTheme);
    visor::ui::render_window(area, buffer, kFrameBounds, display.table());

    VISOR_CHECK_EQ(display.flush_count(), 1);
    VISOR_CHECK_EQ(display.get_pixel(PointI{0, 0}), std::uint32_t{kIrisRgb565});
    VISOR_CHECK_EQ(display.get_pixel(PointI{7, 1}), std::uint32_t{kIrisRgb565});
}

/// The color preset the whole desktop recolors from, one role at a time.
void color_preset_roles() {
    const visor::ui::Theme& preset = visor::ui::kColorTheme;
    VISOR_CHECK_EQ(preset.background, Ink::black());
    VISOR_CHECK_EQ(preset.accent, Ink::from_rgb(kIrisRgb));
    // Every role but the ground is ink on a dark picture: no value is the
    // theme's own alpha, and the accent pair stays readable against each other.
    VISOR_CHECK(preset.foreground != preset.background);
    VISOR_CHECK(preset.on_accent != preset.accent);
    VISOR_CHECK(preset.muted != preset.disabled);
}

} // namespace

void run_pixel_tests() {
    the_default_format_is_mono();
    rgb565_pixels();
    xrgb8888_pixels();
    format_conversions();
    painter_in_color();
    strokes_in_color();
    theme_switch_in_color();
    color_theme_on_mono();
    bitmap_tinted_by_role();
    host_receives_color();
    render_window_submits_the_canvas_format();
    color_preset_roles();
}

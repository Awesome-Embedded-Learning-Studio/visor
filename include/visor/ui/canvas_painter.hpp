#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/pen.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace visor::ui {

/**
 * @brief Binds the runtime Painter interface to one concrete canvas type.
 *
 * The template half of the seam: a CanvasPainter borrows a canvas and a theme,
 * resolves every role to the target's pixel once in its constructor, and builds
 * a Pen per stroke. Widgets never see this type; only the code that owns a
 * canvas does -- a run loop builds one per frame and hands it to the tree as a
 * plain Painter reference.
 *
 * The window is the chapter-eleven addition: the core never assumes the buffer
 * equals the screen, so a painter carries the scene rectangle its buffer
 * stores. Widgets draw in scene coordinates; strokes outside the window are
 * clipped, strokes inside land shifted by the window origin. A full-frame
 * painter is the window case where the scene rectangle is the whole canvas.
 *
 * The role table is the C shape: seven roles resolved once into the target's
 * own pixels, so a fill or a plot pays one lookup, never a per-pixel switch.
 * The translation from Ink to pixel is the canvas's traits table, which is what
 * makes this painter work for every format: the verbs below hold a Pen over
 * CanvasT::Pixel and never name a format. A color canvas therefore keeps the
 * theme's grays, while a mono canvas folds every non-black RGB to lit.
 */
template <typename CanvasT>
class CanvasPainter final : public Painter {
  public:
    using Pixel = CanvasT::Pixel;

    /// Whole-canvas window: the canvas is the entire scene, scene pixel (0, 0)
    /// is the canvas corner.
    explicit CanvasPainter(CanvasT& canvas, const Theme& theme = kLightTheme) noexcept
        : canvas_{canvas}, window_{
                               .pos = base::PointI{0, 0},
                               .size =
                                   base::SizeI{
                                       .w = static_cast<int>(CanvasT::kWidth),
                                       .h = static_cast<int>(CanvasT::kHeight),
                                   },
                           } {
        resolve(theme);
    }

    /// Sub-window: the canvas stores exactly this scene rectangle, its pixel
    /// (0, 0) holds the window's top-left corner. A window may be smaller than
    /// the canvas (the tail strip of a rebuild); the surplus rows are left
    /// untouched and never submitted.
    CanvasPainter(CanvasT& canvas, base::RectI window, const Theme& theme = kLightTheme) noexcept
        : canvas_{canvas}, window_{window} {
        resolve(theme);
    }

    void fill(base::RectI area, canvas::ColorRole role) const noexcept override {
        fill_with(area, pixel(role));
    }

    void fill(base::RectI area, canvas::Ink ink) const noexcept override {
        fill_with(area, CanvasT::Traits::from_ink(ink));
    }

    void fill_rounded(base::RectI area, canvas::ColorRole role,
                      const CornerRadius& radius) const noexcept override {
        rounded_with(area, radius, pixel(role));
    }

    void fill_rounded(base::RectI area, canvas::Ink ink,
                      const CornerRadius& radius) const noexcept override {
        rounded_with(area, radius, CanvasT::Traits::from_ink(ink));
    }

    void draw_image(base::PointI at, const std::uint32_t* words, int width, int height,
                    std::size_t stride_words) const noexcept override {
        // Content imagery, not a role: each source word is its own ink, and
        // the canvas's traits fold it. The rows walk inside the painter's
        // own window, so a strip replay paints exactly its share of the
        // image, and a word outside the window or the canvas never lands.
        if (words == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const base::RectI image{
            .pos = at,
            .size = base::SizeI{.w = width, .h = height},
        };
        const base::RectI visible = base::intersect(image, window_);
        for (int row = visible.pos.y; row < visible.pos.y + visible.size.h; ++row) {
            const std::uint32_t* source =
                words + ((static_cast<std::size_t>(row - at.y) * stride_words) +
                         static_cast<std::size_t>(visible.pos.x - at.x));
            for (int column = visible.pos.x; column < visible.pos.x + visible.size.w; ++column) {
                const base::PointI buffer{
                    column - window_.pos.x,
                    row - window_.pos.y,
                };
                static_cast<void>(
                    canvas_.set(buffer, CanvasT::Traits::from_ink(canvas::Ink{*source++})));
            }
        }
    }

    void draw_bitmap(base::PointI at, const draw::MonoBitmap& source, canvas::ColorRole role,
                     int scale) const noexcept override {
        bitmap_with(at, source, pixel(role), scale);
    }

    void draw_bitmap(base::PointI at, const draw::MonoBitmap& source, canvas::Ink ink,
                     int scale) const noexcept override {
        bitmap_with(at, source, CanvasT::Traits::from_ink(ink), scale);
    }

    void plot(base::PointI at, canvas::ColorRole role) const noexcept override {
        plot_with(at, pixel(role));
    }

    void plot(base::PointI at, canvas::Ink ink) const noexcept override {
        plot_with(at, CanvasT::Traits::from_ink(ink));
    }

  private:
    /// The one stroke per verb, in the target's own pixel: both public
    /// lanes -- the theme's precomputed table and the raw-ink traits fold --
    /// converge here, so the clipping and Pen work lives exactly once.
    /// Same stance as Pen toward out-of-bounds: a stroke outside the
    /// window vanishes silently, stroke algorithms never check bounds.
    void fill_with(base::RectI area, Pixel px) const noexcept {
        const base::RectI visible = base::intersect(area, window_);
        if (visible.is_empty()) {
            return;
        }
        const draw::Pen<CanvasT, Pixel> pen{canvas_, px};
        pen.fill(shift_to_buffer(visible));
    }

    void plot_with(base::PointI at, Pixel px) const noexcept {
        if (!window_.contains(at)) {
            return;
        }
        const draw::Pen<CanvasT, Pixel> pen{canvas_, px};
        pen.plot(base::PointI{at.x - window_.pos.x, at.y - window_.pos.y});
    }

    void rounded_with(base::RectI area, const CornerRadius& radius, Pixel px) const noexcept {
        // One span per row: a row in the upper half takes its insets from
        // the top corners, the lower half from the bottom ones. Within a
        // corner's r x r zone a pixel at (u, v) -- offsets from the corner
        // -- stays dark when its corner point lies outside the quarter
        // disc: (r - u)^2 + (r - v)^2 > r^2. The row's inset is the
        // smallest u that lands back inside (or r when the whole zone row
        // is outside). Radius zero collapses to the plain fill's span, an
        // oversized radius clamps to half the shorter side.
        const int half_short = area.size.w < area.size.h ? area.size.w / 2 : area.size.h / 2;
        const auto clamp = [half_short](std::uint8_t r) {
            const int wanted = r;
            return wanted < half_short ? wanted : half_short;
        };
        const int tl = clamp(radius.top_left);
        const int tr = clamp(radius.top_right);
        const int br = clamp(radius.bottom_right);
        const int bl = clamp(radius.bottom_left);
        const auto inset = [](int r, int v) {
            if (r == 0 || v >= r) {
                return 0; // no zone, or this row is past it
            }
            const long long rr = static_cast<long long>(r) * r;
            for (int u = 0; u < r; ++u) {
                const long long du = r - u;
                const long long dv = r - v;
                if ((du * du) + (dv * dv) <= rr) {
                    return u; // the first u back inside the disc
                }
            }
            return r; // the whole zone row stays dark
        };
        for (int row = 0; row < area.size.h; ++row) {
            const bool upper = row <= (area.size.h - 1 - row);
            const int v = upper ? row : (area.size.h - 1 - row);
            const int inset_left = inset(upper ? tl : bl, v);
            const int inset_right = inset(upper ? tr : br, v);
            if (inset_left + inset_right >= area.size.w) {
                continue; // a fully-rounded row of a capsule paints nothing
            }
            const base::RectI span{
                .pos = base::PointI{area.pos.x + inset_left, area.pos.y + row},
                .size = base::SizeI{.w = area.size.w - inset_left - inset_right, .h = 1},
            };
            fill_with(span, px);
        }
    }

    void bitmap_with(base::PointI at, const draw::MonoBitmap& source, Pixel px,
                     int scale) const noexcept {
        // One lit source pixel is one scale x scale block, at the source
        // coordinate times the factor. Neighbour blocks never overlap (their
        // origins are scale apart), so any order of painting lands the same
        // pixels -- which is what makes strip replays safe here too.
        const int factor = scale < 1 ? 1 : scale;
        const draw::Pen<CanvasT, Pixel> pen{canvas_, px};
        for (std::size_t row = 0; row < source.height; ++row) {
            for (std::size_t column = 0; column < source.width; ++column) {
                if (!source.pixel(column, row)) {
                    continue;
                }
                const base::RectI block{
                    .pos = base::PointI{at.x + (static_cast<int>(column) * factor),
                                        at.y + (static_cast<int>(row) * factor)},
                    .size = base::SizeI{.w = factor, .h = factor},
                };
                const base::RectI visible = base::intersect(block, window_);
                if (visible.is_empty()) {
                    continue;
                }
                pen.fill(shift_to_buffer(visible));
            }
        }
    }

    /// Resolves the seven roles into the target's pixels once per painter.
    /// The Ink-to-pixel step is the canvas's traits table, so the rule that
    /// used to live here as mono_ink now belongs to whatever format the canvas
    /// was instantiated with. A new role without a theme entry falls back to
    /// Foreground through Theme::ink, which keeps the table total while the
    /// switch warns.
    void resolve(const Theme& theme) noexcept {
        for (std::size_t i = 0; i < canvas::kColorRoleCount; ++i) {
            const auto role = static_cast<canvas::ColorRole>(i);
            const canvas::Ink ink = theme.ink(role);
            pixels_[i] = CanvasT::Traits::from_ink(ink);
        }
    }

    [[nodiscard]] Pixel pixel(canvas::ColorRole role) const noexcept {
        return pixels_[static_cast<std::size_t>(role)];
    }

    [[nodiscard]] base::RectI shift_to_buffer(base::RectI area) const noexcept {
        return base::RectI{
            .pos = base::PointI{area.pos.x - window_.pos.x, area.pos.y - window_.pos.y},
            .size = area.size,
        };
    }

    CanvasT& canvas_;
    base::RectI window_;
    std::array<Pixel, canvas::kColorRoleCount> pixels_{};
};

} // namespace visor::ui

#include "visor/ui/widgets/radio/radio.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

RadioGroup::RadioGroup(base::RectI bounds, const draw::MonoFont& font,
                       canvas::ColorRole role) noexcept
    : Widget{bounds}, font_{&font}, role_{role} {}

bool RadioGroup::add_option(std::string_view label) noexcept {
    if (count_ == static_cast<std::size_t>(kRadioCapacity)) {
        return false;
    }
    labels_[count_] = label;
    ++count_;
    return true;
}

int RadioGroup::selected() const noexcept {
    return selected_;
}

void RadioGroup::select(int index) noexcept {
    if (index < 0 || std::cmp_greater_equal(index, count_) || index == selected_) {
        return; // out of range asks nothing, a re-selection changes nothing
    }
    selected_ = index;
    invalidate(bounds());
    changed();
}

void RadioGroup::set_on_change(SelectHandler handler, void* context) noexcept {
    on_change_ = handler;
    change_context_ = context;
}

int RadioGroup::row_height() const noexcept {
    return std::max(1, static_cast<int>(font_->line_height) + 6);
}

int RadioGroup::row_at(base::PointI at) const noexcept {
    if (!bounds().contains(at)) {
        return -1;
    }
    const int row = (at.y - bounds().pos.y) / row_height();
    if (row < 0 || std::cmp_greater_equal(row, count_)) {
        return -1;
    }
    return row;
}

void RadioGroup::changed() noexcept {
    if (on_change_ != nullptr) {
        on_change_(*this, change_context_);
    }
}

void RadioGroup::on_pointer_down(base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {
    // The press on a row IS the selection; the row geometry decides which.
    if (const int row = row_at(at); row >= 0) {
        select(row);
    }
}

void RadioGroup::on_key_down(std::uint32_t code,
                             [[maybe_unused]] host::Modifiers modifiers) noexcept {
    // The arrows walk the selection, clamped at both ends -- the honest
    // refusal to wrap past the column. Enter and Space are nothing here: a
    // radio has no moment to complete, its state moves as it moves.
    if (code == host::kKeyDown) {
        if (selected_ >= 0 && std::cmp_less(selected_ + 1, count_)) {
            select(selected_ + 1);
        }
        return;
    }
    if (code == host::kKeyUp) {
        if (selected_ > 0) {
            select(selected_ - 1);
        }
        return;
    }
}

void RadioGroup::on_draw(const Painter& painter) const noexcept {
    if (bounds().size.w < 11 || bounds().size.h < row_height()) {
        return;
    }
    const auto text = role_;
    for (std::size_t i = 0; i < count_; ++i) {
        const int y = bounds().pos.y + (static_cast<int>(i) * row_height());
        if (y + row_height() > bounds().pos.y + bounds().size.h) {
            break;
        }
        const base::RectI ring{
            .pos = {bounds().pos.x + 2, y + ((row_height() - 7) / 2)},
            .size = {.w = 7, .h = 7},
        };
        // Rounded concentric silhouettes leave a clear gap around the selected dot.
        paint_outline(
            painter, ring, canvas::ColorRole::Border, canvas::ColorRole::Background,
            CornerRadius{.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3});
        if (std::cmp_equal(i, selected_)) {
            painter.fill_rounded(
                base::inset(ring, 2), canvas::ColorRole::Accent,
                CornerRadius{.top_left = 1, .top_right = 1, .bottom_right = 1, .bottom_left = 1});
        }
        const base::PointI label_pos{
            ring.pos.x + 11, y + ((row_height() - static_cast<int>(font_->line_height)) / 2)};
        draw_text_clipped(painter, base::inset(bounds(), 1), label_pos, *font_, labels_[i], text,
                          1);
    }
}

} // namespace visor::ui

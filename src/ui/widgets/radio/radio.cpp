#include "visor/ui/widgets/radio/radio.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

RadioGroup::RadioGroup(base::RectI bounds, const draw::MonoFont& font, canvas::Color ink) noexcept
    : Widget{bounds}, font_{&font}, ink_{ink} {}

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
    return static_cast<int>(font_->line_height);
}

int RadioGroup::row_at(base::PointI at) const noexcept {
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

void RadioGroup::on_pointer_down(base::PointI at) noexcept {
    // The press on a row IS the selection; the row geometry decides which.
    if (const int row = row_at(at); row >= 0) {
        select(row);
    }
}

void RadioGroup::on_key_down(std::uint32_t code) noexcept {
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
    for (std::size_t i = 0; i < count_; ++i) {
        const int y = bounds().pos.y + (static_cast<int>(i) * row_height());
        // The ring: a five-pixel square outline, four one-pixel fills.
        const base::PointI ring_pos{bounds().pos.x, y + (row_height() / 2) - 2};
        painter.fill(base::RectI{.pos = ring_pos, .size = base::SizeI{.w = 5, .h = 1}}, ink_);
        painter.fill(base::RectI{.pos = base::PointI{ring_pos.x, ring_pos.y + 4},
                                 .size = base::SizeI{.w = 5, .h = 1}},
                     ink_);
        painter.fill(base::RectI{.pos = ring_pos, .size = base::SizeI{.w = 1, .h = 5}}, ink_);
        painter.fill(base::RectI{.pos = base::PointI{ring_pos.x + 4, ring_pos.y},
                                 .size = base::SizeI{.w = 1, .h = 5}},
                     ink_);
        // The dot on the chosen option: a two-pixel core inside the ring.
        if (std::cmp_equal(i, selected_)) {
            painter.fill(base::RectI{.pos = base::PointI{ring_pos.x + 2, ring_pos.y + 2},
                                     .size = base::SizeI{.w = 2, .h = 2}},
                         ink_);
        }
        // The label rides beside the ring, vertically centered on the row.
        const base::PointI label_pos{
            ring_pos.x + 8, y + ((row_height() - static_cast<int>(font_->line_height)) / 2)};
        draw_text(painter, label_pos, *font_, labels_[i], ink_, 1);
    }
}

} // namespace visor::ui

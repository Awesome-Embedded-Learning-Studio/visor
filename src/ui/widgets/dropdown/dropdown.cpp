#include "visor/ui/widgets/dropdown/dropdown.hpp"

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

Dropdown::Dropdown(base::RectI bounds, const draw::MonoFont& font, canvas::Color ink) noexcept
    : Widget{bounds}, closed_bounds_{bounds}, font_{&font}, ink_{ink} {}

bool Dropdown::add_option(std::string_view label) noexcept {
    if (count_ == static_cast<std::size_t>(kDropdownCapacity)) {
        return false;
    }
    labels_[count_] = label;
    ++count_;
    return true;
}

int Dropdown::selected() const noexcept {
    return selected_;
}

bool Dropdown::open() const noexcept {
    return open_;
}

void Dropdown::select(int index) noexcept {
    if (index < 0 || std::cmp_greater_equal(index, count_) || index == selected_) {
        return; // out of range asks nothing, a re-selection changes nothing
    }
    selected_ = index;
    invalidate(bounds());
    changed();
}

void Dropdown::set_on_change(SelectHandler handler, void* context) noexcept {
    on_change_ = handler;
    change_context_ = context;
}

int Dropdown::row_height() const noexcept {
    return closed_bounds_.size.h;
}

int Dropdown::row_at(base::PointI at) const noexcept {
    if (!open_) {
        return -1;
    }
    const int row = (at.y - bounds().pos.y) / row_height();
    if (row < 0 || std::cmp_greater_equal(row, count_)) {
        return -1;
    }
    return row;
}

void Dropdown::set_open(bool on) noexcept {
    if (on == open_ || count_ == 0) {
        return; // already there, or nothing to show
    }
    open_ = on;
    if (on) {
        // Grow downward over the options; set_bounds reports both boxes, so
        // the overlap with siblings is an owed repaint, never a silent one.
        highlighted_ = selected_ >= 0 ? selected_ : 0;
        set_bounds(base::RectI{
            .pos = closed_bounds_.pos,
            .size =
                base::SizeI{
                    .w = closed_bounds_.size.w,
                    .h = row_height() * static_cast<int>(count_),
                },
        });
    } else {
        highlighted_ = -1;
        set_bounds(closed_bounds_);
    }
}

void Dropdown::changed() noexcept {
    if (on_change_ != nullptr) {
        on_change_(*this, change_context_);
    }
}

void Dropdown::on_pointer_down(base::PointI at) noexcept {
    if (!open_) {
        set_open(true);
        return;
    }
    // A press on an open row picks it and closes in one step.
    if (const int row = row_at(at); row >= 0) {
        select(row);
        set_open(false);
    }
}

void Dropdown::on_key_down(std::uint32_t code) noexcept {
    if (!open_) {
        // Space also opens as its code point: a text host types 0x20 for the
        // space bar, the same press under either number.
        if (code == host::kKeyEnter || code == host::kKeySpace || code == host::kCharSpace) {
            set_open(true);
        }
        return;
    }
    if (code == host::kKeyDown) {
        if (highlighted_ >= 0 && std::cmp_less(highlighted_ + 1, count_)) {
            ++highlighted_;
            invalidate(bounds()); // the highlight moved, the pixels owe
        }
        return;
    }
    if (code == host::kKeyUp) {
        if (highlighted_ > 0) {
            --highlighted_;
            invalidate(bounds());
        }
        return;
    }
    if (code == host::kKeyEnter) {
        if (highlighted_ >= 0) {
            select(highlighted_);
        }
        set_open(false);
    }
}

void Dropdown::on_draw(const Painter& painter) const noexcept {
    // The closed row: the selected label (or nothing, if none), a marker
    // column of three shrinking rows on the right, a frame all around.
    const canvas::Color other = ink_ == canvas::Color::On ? canvas::Color::Off : canvas::Color::On;
    if (!open_) {
        painter.fill(base::RectI{.pos = bounds().pos, .size = bounds().size}, other);
        const base::PointI label_pos{
            bounds().pos.x + 1,
            bounds().pos.y + ((row_height() - static_cast<int>(font_->line_height)) / 2)};
        if (selected_ >= 0) {
            draw_text(painter, label_pos, *font_, labels_[selected_], ink_, 1);
        }
        // The marker: three rows, one pixel narrower each -- a plain arrow
        // pointing where the list will grow.
        for (int step = 0; step < 3; ++step) {
            painter.fill(
                base::RectI{
                    .pos = base::PointI{bounds().pos.x + bounds().size.w - 4 + step,
                                        bounds().pos.y + 1 + step},
                    .size = base::SizeI{.w = 3 - step, .h = 1},
                },
                ink_);
        }
        return;
    }
    // The open list: option rows, the highlighted one inverted.
    for (std::size_t i = 0; i < count_; ++i) {
        const int y = bounds().pos.y + (static_cast<int>(i) * row_height());
        const bool lit = std::cmp_equal(i, highlighted_);
        painter.fill(
            base::RectI{
                .pos = base::PointI{bounds().pos.x, y},
                .size = base::SizeI{.w = bounds().size.w, .h = row_height()},
            },
            lit ? ink_ : other);
        const base::PointI label_pos{
            bounds().pos.x + 1, y + ((row_height() - static_cast<int>(font_->line_height)) / 2)};
        draw_text(painter, label_pos, *font_, labels_[i], lit ? other : ink_, 1);
    }
}

} // namespace visor::ui

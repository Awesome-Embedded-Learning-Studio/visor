#include "visor/ui/widgets/dropdown/dropdown.hpp"

#include <algorithm>
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
#include "visor/ui/control_style.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/popup_layer.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

Dropdown::Dropdown(base::RectI bounds, const draw::MonoFont& font, canvas::ColorRole role) noexcept
    : Widget{bounds}, closed_bounds_{bounds}, font_{&font}, role_{role} {}

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
    if (popup_ != nullptr) {
        return popup_->content() == &list_;
    }
    return open_;
}

void Dropdown::set_popup(PopupLayer* popup) noexcept {
    if (popup_ == popup) {
        return;
    }
    set_open(false);
    popup_ = popup;
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
    return std::max(1, closed_bounds_.size.h);
}

int Dropdown::row_at(base::PointI at) const noexcept {
    if (!open_ || !bounds().contains(at)) {
        return -1;
    }
    const int row = (at.y - bounds().pos.y) / row_height();
    if (row < 0 || std::cmp_greater_equal(row, count_)) {
        return -1;
    }
    return row;
}

void Dropdown::set_open(bool on) noexcept {
    if (count_ == 0) {
        return; // nothing to show
    }
    if (popup_ != nullptr) {
        if (on) {
            if (open()) {
                return;
            }
            const int row_h = std::max(1, bounds().size.h);
            highlighted_ = selected_ >= 0 ? selected_ : 0;
            list_.set_bounds(base::RectI{
                .pos = base::PointI{bounds().pos.x, bounds().pos.y + bounds().size.h},
                .size =
                    base::SizeI{
                        .w = bounds().size.w,
                        .h = row_h * static_cast<int>(count_),
                    },
            });
            if (!popup_->open(&list_)) {
                highlighted_ = -1;
            }
            return;
        }
        if (popup_->content() == &list_) {
            static_cast<void>(popup_->close());
        }
        highlighted_ = -1;
        return;
    }

    if (on == open_) {
        return; // already there
    }
    open_ = on;
    invalidate(bounds());
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

void Dropdown::on_pointer_down(base::PointI at,
                               [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (popup_ != nullptr) {
        // The popup layer owns the open list and blocks the closed row while
        // it is up; a press here means the closed row was pressed.
        if (!open()) {
            set_open(true);
        }
        return;
    }
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

void Dropdown::on_key_down(std::uint32_t code,
                           [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (!open()) {
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
            invalidate_open_list();
        }
        return;
    }
    if (code == host::kKeyUp) {
        if (highlighted_ > 0) {
            --highlighted_;
            invalidate_open_list();
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

void Dropdown::draw_rows(const Painter& painter, base::RectI area, int row_h) const noexcept {
    const auto text = role_;
    for (std::size_t i = 0; i < count_; ++i) {
        const int y = area.pos.y + (static_cast<int>(i) * row_h);
        const bool highlighted = std::cmp_equal(i, highlighted_);
        const base::RectI row{
            .pos = {area.pos.x + 2, y + 2},
            .size = {.w = std::max(0, area.size.w - 4), .h = std::max(0, row_h - 4)},
        };
        painter.fill(row, highlighted ? canvas::ColorRole::Accent : canvas::ColorRole::Background);
        draw_text_clipped(painter, row,
                          {row.pos.x + 2, y + ((row_h - static_cast<int>(font_->line_height)) / 2)},
                          *font_, labels_[i], highlighted ? canvas::ColorRole::OnAccent : text, 1);
    }
}

void Dropdown::invalidate_open_list() noexcept {
    if (popup_ != nullptr) {
        list_.refresh();
        return;
    }
    invalidate(bounds());
}

void Dropdown::on_draw(const Painter& painter) const noexcept {
    const auto text = role_;
    const auto corners = theme() != nullptr ? theme()->corner_radius : CornerRadius{};
    paint_outline(painter, bounds(), canvas::ColorRole::Border, canvas::ColorRole::Background,
                  corners);
    if (popup_ != nullptr || !open_) {
        const base::RectI content{
            .pos = {bounds().pos.x + 4, bounds().pos.y + 1},
            .size =
                {
                    .w = std::max(0, bounds().size.w - 16),
                    .h = std::max(0, bounds().size.h - 2),
                },
        };
        if (selected_ >= 0) {
            draw_text_clipped(
                painter, content,
                {content.pos.x,
                 bounds().pos.y + ((row_height() - static_cast<int>(font_->line_height)) / 2)},
                *font_, labels_[selected_], text, 1);
        }
        if (bounds().size.w >= 12 && bounds().size.h >= 7) {
            const int x = bounds().pos.x + bounds().size.w - 9;
            const int y = bounds().pos.y + (bounds().size.h / 2) - 1;
            for (int step = 0; step < 3; ++step) {
                painter.fill(
                    base::RectI{.pos = {x + step, y + step}, .size = {.w = 5 - (2 * step), .h = 1}},
                    text);
            }
        }
        return;
    }
    draw_rows(painter, bounds(), row_height());
}

Dropdown::List::List(Dropdown& owner) noexcept : Widget{base::RectI{}}, owner_{owner} {}

int Dropdown::List::row_at(base::PointI at) const noexcept {
    if (!bounds().contains(at)) {
        return -1;
    }
    const int row_h = std::max(1, owner_.bounds().size.h);
    const int row = (at.y - bounds().pos.y) / row_h;
    if (row < 0 || std::cmp_greater_equal(row, owner_.count_)) {
        return -1;
    }
    return row;
}

void Dropdown::List::on_pointer_down(base::PointI at,
                                     [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (const int row = row_at(at); row >= 0) {
        owner_.select(row);
    }
    // A press on the list's padding closes without a pick, the same as a
    // press outside it.
    owner_.set_open(false);
}

void Dropdown::List::on_draw(const Painter& painter) const noexcept {
    const auto corners = owner_.theme() != nullptr ? owner_.theme()->corner_radius : CornerRadius{};
    paint_outline(painter, bounds(), canvas::ColorRole::Border, canvas::ColorRole::Background,
                  corners);
    owner_.draw_rows(painter, bounds(), std::max(1, owner_.bounds().size.h));
}

} // namespace visor::ui

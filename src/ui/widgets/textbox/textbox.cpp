#include "visor/ui/widgets/textbox/textbox.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

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

TextBox::TextBox(base::RectI bounds, const draw::MonoFont& font, canvas::Color ink) noexcept
    : Widget{bounds}, font_{&font}, ink_{ink} {}

std::string_view TextBox::text() const noexcept {
    return {chars_, length_};
}

void TextBox::set_text(std::string_view text) noexcept {
    const std::size_t take = text.size() < kTextBoxCapacity ? text.size() : kTextBoxCapacity;
    // Judge before copying: comparing after the copy would compare the text
    // with itself and never see a change.
    if (take == length_) {
        bool same = true;
        for (std::size_t i = 0; i < take; ++i) {
            same = same && chars_[i] == text[i];
        }
        if (same) {
            return; // same words, same pixels, no debt
        }
    }
    for (std::size_t i = 0; i < take; ++i) {
        chars_[i] = text[i];
    }
    length_ = take;
    caret_ = length_;
    fit_window();
    invalidate(bounds());
    changed();
}

void TextBox::set_on_change(TextHandler handler, void* context) noexcept {
    on_change_ = handler;
    change_context_ = context;
}

void TextBox::set_on_submit(TextHandler handler, void* context) noexcept {
    on_submit_ = handler;
    submit_context_ = context;
}

int TextBox::caret_offset(std::size_t from) const noexcept {
    return draw::measure_text(*font_, std::string_view{chars_ + from, caret_ - from}, 1).w;
}

void TextBox::fit_window() noexcept {
    const int room = bounds().size.w - kCaretWidth;
    while (first_visible_ < caret_ && caret_offset(first_visible_) > room) {
        ++first_visible_;
    }
    // Pull back symmetrically: deletion may have moved the caret left of the
    // window, and the run before the caret gets to come back too -- as much
    // of it as fits, the same budget the forward slide spends.
    while (first_visible_ > 0 && caret_offset(first_visible_ - 1) <= room) {
        --first_visible_;
    }
}

void TextBox::changed() noexcept {
    if (on_change_ != nullptr) {
        on_change_(*this, change_context_);
    }
}

void TextBox::on_key_down(std::uint32_t code) noexcept {
    if (code >= 0x20 && code <= 0x7e && length_ < kTextBoxCapacity) {
        // Insert at the caret: the tail steps right one byte at a time, no
        // memmove, no vocabulary the freestanding world cannot carry.
        for (std::size_t i = length_; i > caret_; --i) {
            chars_[i] = chars_[i - 1];
        }
        chars_[caret_] = static_cast<char>(code);
        ++length_;
        ++caret_;
        fit_window();
        invalidate(bounds());
        changed();
        return;
    }
    if (code == host::kKeyBackspace && caret_ > 0) {
        --caret_;
        for (std::size_t i = caret_; i + 1 < length_; ++i) {
            chars_[i] = chars_[i + 1];
        }
        --length_;
        fit_window();
        invalidate(bounds());
        changed();
        return;
    }
    if (code == host::kKeyLeft && caret_ > 0) {
        --caret_;
        fit_window();
        invalidate(bounds()); // the caret moved, the pixels owe
        return;
    }
    if (code == host::kKeyRight && caret_ < length_) {
        ++caret_;
        fit_window();
        invalidate(bounds());
        return;
    }
    if (code == host::kKeyEnter) {
        if (on_submit_ != nullptr) {
            on_submit_(*this, submit_context_);
        }
        return;
    }
    // Up, Down and every host-private code: a single line has no use for
    // them, and the honest editor ignores what it cannot name.
}

void TextBox::on_draw(const Painter& painter) const noexcept {
    const std::string_view visible{chars_ + first_visible_, length_ - first_visible_};
    draw_text(painter, bounds().pos, *font_, visible, ink_, 1);
    if (!focused()) {
        return; // an unfocused editor states its text, no caret to show
    }
    const int x = bounds().pos.x + caret_offset(first_visible_);
    painter.fill(base::RectI{.pos = base::PointI{x, bounds().pos.y},
                             .size = base::SizeI{.w = kCaretWidth, .h = bounds().size.h}},
                 ink_);
}

} // namespace visor::ui

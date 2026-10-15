#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/// The single-line editor's storage: a fixed byte buffer, no heap, no
/// std::string. Capacity is a compile-time fact like the tree's child bound;
/// typing past it is dropped, not truncated silently -- the drop is the
/// documented behavior, one constant governs every instance.
inline constexpr std::size_t kTextBoxCapacity = 64;

/**
 * @brief A single-line text editor, the keyboard's first writing control.
 *
 * Text arrives as the KeyDown convention events.hpp states: a host that
 * produces text maps printable keys to their code points, so printable ASCII
 * (0x20 through 0x7E) lands as characters. The editing semantics ride the
 * core's named codes: Backspace deletes before the caret, Left and Right
 * move it (Up and Down are a single line's non-events, ignored), Enter fires
 * the submit handler without inserting anything. Every other code is
 * ignored, the same honest refusal the other controls take.
 *
 * The buffer is ASCII-bytes for now: one char, one byte, one glyph advance.
 * Wide input (CJK through a text-input convention) is a later layer on the
 * same storage, not this one's business.
 *
 * Drawing is a window over the text: the box shows a run of the buffer with
 * the caret always inside its own width, the run sliding forward when the
 * caret would pass the right edge and back when deletion pulls it left of
 * the window. The caret is a one-pixel ink column after the visible prefix,
 * painted only while the box holds the focus -- an unfocused editor states
 * its text and takes no input.
 *
 * Feedback is the Button shape: plain functions plus context words, on_change
 * per edit and on_submit per Enter.
 */
class TextBox : public Widget {
  public:
    /// @brief What an edit (or an Enter) calls. context is the word handed
    ///        to the setter, given back untouched.
    using TextHandler = void (*)(TextBox& self, void* context);

    TextBox(base::RectI bounds, const draw::MonoFont& font, canvas::Color ink) noexcept;

    /// The text so far, as a view over the fixed buffer.
    [[nodiscard]] std::string_view text() const noexcept;

    /// Replaces the text (truncating at capacity), caret to the end, window
    /// pulled to fit the tail. A changed text reports and fires on_change.
    void set_text(std::string_view text) noexcept;

    void set_on_change(TextHandler handler, void* context = nullptr) noexcept;
    void set_on_submit(TextHandler handler, void* context = nullptr) noexcept;

    /// An editor takes the keyboard: focus traversal may land here.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    void on_key_down(std::uint32_t code) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    static constexpr int kPadding = 3;

    /// The caret column's width in pixels.
    static constexpr int kCaretWidth = 1;

    /// The pixel width of chars_[from, caret_) in the font.
    [[nodiscard]] int caret_offset(std::size_t from) const noexcept;
    /// Slides the window so the caret sits inside the box's width.
    void fit_window() noexcept;
    /// Fires the change handler.
    void changed() noexcept;

    // The fixed buffer IS the design: no heap, no indirection, one constant
    // governing every editor.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    char chars_[kTextBoxCapacity]{};
    std::size_t length_ = 0;
    std::size_t caret_ = 0;         // insertion index, 0 through length_
    std::size_t first_visible_ = 0; // window start, caret_ >= first_visible_
    const draw::MonoFont* font_;
    canvas::Color ink_;
    TextHandler on_change_ = nullptr;
    void* change_context_ = nullptr;
    TextHandler on_submit_ = nullptr;
    void* submit_context_ = nullptr;
};

} // namespace visor::ui

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/// The option list's fixed capacity, one constant for every group (the
/// tree's child bound takes the same stance). Adding past it is refused and
/// reported by the return value, the add_child way.
inline constexpr std::size_t kRadioCapacity = 8;

/**
 * @brief A column of exclusive options: one ring per option, one dot on the
 *        chosen one.
 *
 * The group is one widget, not a box of sibling controls: exclusivity is the
 * group's fact, so the selection lives here and no child can drift out of
 * step. Options are labels over caller-owned storage (the label view
 * discipline). Rows use the font's line height plus six pixels. A seven-pixel
 * rounded ring surrounds the selected dot. Bounds must be at least eleven
 * pixels wide; rows that do not fit entirely inside the bounds are omitted.
 *
 * Selecting is the whole interaction, so it has no separate activation:
 * a pointer press on a row selects it, and the arrows the core names (Up
 * and Down, single column) move the selection with clamping at both ends --
 * the first controls to consume the arrow keys TextBox declined. Enter and
 * Space are deliberately nothing here: unlike a button, a radio has no
 * moment to complete; its state changes as it moves.
 *
 * Feedback is the plain-function-plus-context shape: on_change fires per
 * selection change, never for a re-selection of the same row.
 */
class RadioGroup : public Widget {
  public:
    /// @brief What a selection change calls. context is the word handed to
    ///        set_on_change, given back untouched.
    using SelectHandler = void (*)(RadioGroup& self, void* context);

    RadioGroup(base::RectI bounds, const draw::MonoFont& font,
               canvas::ColorRole role = canvas::ColorRole::Foreground) noexcept;

    /// Adds an option; false when the capacity is full. Labels are views:
    /// the storage behind them outlives the group.
    [[nodiscard]] bool add_option(std::string_view label) noexcept;

    /// The selected option's index, -1 while none is (an empty group, or
    /// one nobody has touched).
    [[nodiscard]] int selected() const noexcept;

    /// Selects by index, clamped to silence: out-of-range asks for nothing.
    /// A changed selection reports the bounds as owed repaint and fires the
    /// handler; re-selecting the current row changes nothing.
    void select(int index) noexcept;

    void set_on_change(SelectHandler handler, void* context = nullptr) noexcept;

    /// A radio group takes the keyboard: the arrows walk the options.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;
    void on_key_down(std::uint32_t code, host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    /// One option row's height: the font's line height plus six pixels of padding.
    [[nodiscard]] int row_height() const noexcept;
    /// The option index at a point in the group, -1 past the last row.
    [[nodiscard]] int row_at(base::PointI at) const noexcept;
    /// Fires the change handler.
    void changed() noexcept;

    // The fixed label table IS the design (the TextBox buffer's stance):
    // no heap, no indirection, one constant governing every group.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    std::string_view labels_[kRadioCapacity]{};
    std::size_t count_ = 0;
    int selected_ = -1;
    const draw::MonoFont* font_;
    canvas::ColorRole role_;
    SelectHandler on_change_ = nullptr;
    void* change_context_ = nullptr;
};

} // namespace visor::ui

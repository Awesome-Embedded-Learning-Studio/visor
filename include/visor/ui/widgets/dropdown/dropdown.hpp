#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

class PopupLayer;

/// The option list's fixed capacity, the radio group's stance.
inline constexpr std::size_t kDropdownCapacity = 8;

/**
 * @brief A collapsed choice: one row showing the current option, opening
 *        into the list it stands on.
 *
 * Closed, the dropdown is its bounds: the selected option's label and a
 * marker column on the right. Open, it grows downward by set_bounds -- the
 * layout's own tool, so the growth and the collapse each report both boxes
 * as owed repaint and no sibling ever sees a silent overlap. The open list
 * paints option rows with the highlight inverted (a filled row, the label
 * in the opposite ink), the closed row never inverts.
 *
 * A dropdown may instead hand its open list to a PopupLayer: set_popup()
 * names the layer, and from then on open() mounts the list on that full-screen
 * layer without touching the dropdown's own bounds. The list is a member, so
 * it costs no heap and lives as long as the control; the layer keeps it above
 * the window it came from and outside that window's geometry. Without a
 * popup layer the original in-tree growth stays available, which keeps
 * existing controls and judgments on their old road. A popup layer must
 * outlive the dropdowns it serves, and a dropdown must be closed before it
 * is destroyed; the control does not touch the layer from its destructor.
 *
 * The interaction is two verbs on one control: open (a press on the closed
 * row, or Enter or Space while focused) and pick (a press on an open row,
 * or Enter on the highlighted row -- Up and Down move that highlight while
 * open, clamped at both ends). Picking selects and closes in one step;
 * opening when nothing is selected highlights the first option.
 *
 * Options are labels over caller-owned storage (the label view discipline),
 * selection reports and fires on_change per change, the plain function plus
 * context word shape throughout.
 */
class Dropdown : public Widget {
  public:
    /// @brief What a selection change calls. context is the word handed to
    ///        set_on_change, given back untouched.
    using SelectHandler = void (*)(Dropdown& self, void* context);

    Dropdown(base::RectI bounds, const draw::MonoFont& font,
             canvas::ColorRole role = canvas::ColorRole::Foreground) noexcept;

    /// Adds an option; false when the capacity is full. Labels are views:
    /// the storage behind them outlives the control.
    [[nodiscard]] bool add_option(std::string_view label) noexcept;

    /// The selected option's index, -1 while none is.
    [[nodiscard]] int selected() const noexcept;

    /// Whether the list is currently open. With a popup layer this asks the
    /// layer whether the member list is the content it currently holds.
    [[nodiscard]] bool open() const noexcept;

    /// Attaches or detaches the popup layer. With a layer, opening mounts the
    /// member list on it; without one, the in-tree growth stays. Changing the
    /// layer closes whatever is open first.
    void set_popup(PopupLayer* popup) noexcept;

    /// Opens or closes the list. With a popup layer this mounts or unmounts
    /// the member list and leaves the dropdown's bounds alone; without one it
    /// grows or restores the bounds by the same road the interactions take
    /// (both boxes owed repaint). Opening an empty list asks nothing.
    void set_open(bool on) noexcept;

    /// Selects by index (out of range asks nothing). A changed selection
    /// reports the bounds as owed repaint and fires the handler;
    /// re-selecting the current option changes nothing.
    void select(int index) noexcept;

    void set_on_change(SelectHandler handler, void* context = nullptr) noexcept;

    /// A dropdown takes the keyboard: Enter and Space open it, the arrows
    /// walk the open list, Enter picks.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;
    void on_key_down(std::uint32_t code, host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    /// One option row's height: the closed row's own height, which the
    /// constructor's bounds sized for exactly one label.
    [[nodiscard]] int row_height() const noexcept;
    /// The option index at a point, -1 outside the option rows.
    [[nodiscard]] int row_at(base::PointI at) const noexcept;
    /// The bounds the constructor laid out, restored on close.
    base::RectI closed_bounds_;
    /// Fires the change handler.
    void changed() noexcept;

    /// The list the popup layer mounts. A nested control so it can read the
    /// dropdown's label table and draw with the dropdown's own theme.
    class List final : public Widget {
      public:
        explicit List(Dropdown& owner) noexcept;

        /// Reports the list's own pixels as owed; the enclosing dropdown
        /// cannot call the protected invalidate() on another widget.
        void refresh() noexcept { invalidate(bounds()); }

        void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;

      protected:
        void on_draw(const Painter& painter) const noexcept override;

      private:
        [[nodiscard]] int row_at(base::PointI at) const noexcept;
        Dropdown& owner_;
    };

    /// Paints one row per option inside area: the highlight inverted, the
    /// label in the opposite ink.
    void draw_rows(const Painter& painter, base::RectI area, int row_h) const noexcept;
    /// Reports the open list's pixels as owed: the member list in popup mode,
    /// the grown bounds otherwise.
    void invalidate_open_list() noexcept;

    // The fixed label table IS the design (the TextBox buffer's stance):
    // no heap, no indirection, one constant governing every dropdown.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    std::string_view labels_[kDropdownCapacity]{};
    std::size_t count_ = 0;
    int selected_ = -1;
    int highlighted_ = -1;
    bool open_ = false;
    const draw::MonoFont* font_;
    canvas::ColorRole role_;
    SelectHandler on_change_ = nullptr;
    void* change_context_ = nullptr;
    PopupLayer* popup_ = nullptr;
    List list_{*this};
};

} // namespace visor::ui

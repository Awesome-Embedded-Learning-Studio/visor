#pragma once

#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief The first toggling control: each press flips it, the mark shows it.
 *
 * The flip happens on press, not on release. The dispatcher carries no click
 * synthesis, so a press-time flip is the honest mapping of "poked" onto the
 * two pointer words, and it is what a touch screen feels like. Release then
 * changes nothing: unlike Button there is no transient state to undo.
 */
class Checkbox : public Widget {
  public:
    using Widget::Widget;

    /// Checkboxes take the keyboard: focus traversal may land here.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    [[nodiscard]] bool checked() const noexcept;
    void on_pointer_down() noexcept override;
    /// Enter and Space flip the box, the keyboard's press; other codes pass.
    void on_key_down(std::uint32_t code) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    bool checked_ = false;
};

} // namespace visor::ui

#pragma once

#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief The first stateful control: lights up while pressed, restores on release.
 *
 * The press handlers carry no position on purpose: hit testing already decided
 * which widget receives the event, so the button only flips its state. The
 * pixel difference between the two states is the button's whole feedback.
 *
 * No keyboard activation yet, on purpose: a momentary button needs a frame
 * that shows the pressed state between down and up, which needs the timing
 * story (key repeat or a tick) that no batch has brought so far. Checkbox
 * activates today because a toggle persists and paints by itself.
 */
class Button : public Widget {
  public:
    using Widget::Widget;

    /// Buttons take the keyboard: focus traversal may land here.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    [[nodiscard]] bool pressed() const noexcept;
    void on_pointer_down() noexcept override;
    void on_pointer_up() noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    bool pressed_ = false;
};

} // namespace visor::ui

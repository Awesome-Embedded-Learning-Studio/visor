#pragma once

#include "visor/ui/widget.hpp"

namespace visor::ui {

/**
 * @brief The first stateful control: lights up while pressed, restores on release.
 *
 * The press handlers carry no position on purpose: hit testing already decided
 * which widget receives the event, so the button only flips its state. The
 * pixel difference between the two states is the button's whole feedback.
 */
class Button : public Widget {
  public:
    using Widget::Widget;

    [[nodiscard]] bool pressed() const noexcept;
    void on_pointer_down() noexcept override;
    void on_pointer_up() noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    bool pressed_ = false;
};

} // namespace visor::ui

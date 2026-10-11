#pragma once

#include "visor/canvas/color.hpp"
#include "visor/ui/widget.hpp"

namespace visor::ui {

/**
 * @brief The simplest decoration: a filled rectangle with no state and no events.
 *
 * Its public ink member is deliberate -- a decoration has nothing to protect,
 * and tests set the color directly.
 */
class Rectangle : public Widget {
  public:
    using Widget::Widget;

    canvas::Color fill = canvas::Color::On;

  protected:
    void on_draw(const Painter& painter) const noexcept override;
};

} // namespace visor::ui

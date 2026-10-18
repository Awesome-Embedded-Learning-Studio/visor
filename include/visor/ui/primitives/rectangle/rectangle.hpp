#pragma once

#include "visor/canvas/color.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief The simplest decoration: a filled rectangle with no state and no events.
 *
 * Its public role member is deliberate -- a decoration has nothing to protect,
 * and tests set the role directly.
 */
class Rectangle : public Widget {
  public:
    using Widget::Widget;

    canvas::ColorRole fill = canvas::ColorRole::Foreground;

  protected:
    void on_draw(const Painter& painter) const noexcept override;
};

} // namespace visor::ui

#pragma once

#include <cstdint>

#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief An invisible container that stacks its children in a row.
 *
 * HBox paints nothing and parents children: the arrangement IS the widget.
 * layout() walks the children in add order, keeps each child's main-axis
 * extent, stretches its cross-axis extent to the box's own, and steps the
 * main cursor past it plus the spacing. Invisible children still take their
 * slot -- hiding is a paint-and-input matter, arrangement is structural.
 *
 * Relayout is the caller's call, never automatic: after adding children,
 * after a child's extent changes, or after the box itself is set_bounds'd,
 * run layout() again (nested boxes run outer first, then the inner boxes
 * whose bounds the outer pass just set). Every move the layout makes rides
 * set_bounds, so the old and new boxes are reported as owed repaint -- no
 * rearrangement is ever silent.
 */
class HBox : public Widget {
  public:
    explicit HBox(base::RectI bounds) noexcept;

    /// The gap between consecutive children, in pixels along the main axis.
    void set_spacing(int spacing) noexcept;

    /// Arranges the children along the row. See the class comment for when.
    void layout() noexcept;

  protected:
    void on_draw([[maybe_unused]] const Painter& painter) const noexcept override {}

  private:
    int spacing_ = 0;
};

/**
 * @brief An invisible container that stacks its children in a column.
 *
 * The same arrangement as HBox with the axes swapped: children keep their
 * height, stretch to the box's width, stack top to bottom with spacing.
 */
class VBox : public Widget {
  public:
    explicit VBox(base::RectI bounds) noexcept;

    /// The gap between consecutive children, in pixels along the main axis.
    void set_spacing(int spacing) noexcept;

    /// Arranges the children along the column. See HBox for when.
    void layout() noexcept;

  protected:
    void on_draw([[maybe_unused]] const Painter& painter) const noexcept override {}

  private:
    int spacing_ = 0;
};

} // namespace visor::ui

#pragma once

#include <array>
#include <cstddef>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

/// Fixed child capacity every widget shares. One compile-time constant keeps the
/// tree allocation-free; if a real control ever needs a different bound, this
/// becomes a template parameter then, with the first demand in hand.
inline constexpr std::size_t kMaxChildren = 8;

/**
 * @brief Base class of every control: a bounding box, a visibility switch and a
 *        fixed-capacity child list.
 *
 * Widgets are runtime objects by spec: their state changes as events arrive, and
 * templating the whole scene would rebuild the program for every layout. What
 * stays compile-time is the raster profile below them, reached through Painter.
 *
 * Two walking orders live on one tree. draw() goes pre-order -- parent first,
 * children on top of it -- because later pixels overwrite earlier ones. hit_test()
 * walks the same tree backwards so the widget that paints last is the one that
 * receives the click.
 *
 * The replay contract: on_draw() paints and nothing else. It must not read
 * events, advance animation or mutate state, because strip rendering will draw
 * the same widget several times per frame and only a stateless replay keeps
 * every pass identical.
 */
class Widget {
  public:
    explicit Widget(base::RectI bounds) noexcept;
    virtual ~Widget() = default;

    // A widget in a tree is an identity, not a value: copying one would silently
    // share its children pointers between two parents.
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    [[nodiscard]] const base::RectI& bounds() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    void set_visible(bool on) noexcept;

    /// Adopts a child; returns false when the capacity is full or child is null.
    [[nodiscard]] bool add_child(Widget* child) noexcept;
    [[nodiscard]] std::size_t child_count() const noexcept;

    /// Draws this subtree pre-order, skipping every invisible widget.
    void draw(const Painter& painter) const noexcept;

    /// Returns the widget that receives a click at this point, walking children
    /// in reverse add order; null when the point lands outside this subtree or
    /// on an invisible branch.
    [[nodiscard]] Widget* hit_test(base::PointI at) noexcept;

  protected:
    /// Paints this widget only; the tree walk around it belongs to draw().
    virtual void on_draw(const Painter& painter) const noexcept = 0;

  private:
    base::RectI bounds_;
    bool visible_ = true;
    std::array<Widget*, kMaxChildren> children_{};
    std::size_t child_count_ = 0;
};

} // namespace visor::ui

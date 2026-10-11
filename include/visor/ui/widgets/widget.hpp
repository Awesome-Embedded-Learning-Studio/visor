#pragma once

#include <array>
#include <cstddef>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/ui/dirty_regions.hpp"
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
 * Focus lives here too: at most one widget holds the keyboard focus at a time,
 * draw() paints the one-pixel ring for the holder, and advance_focus() walks
 * draw order to hand it on when Tab arrives.
 *
 * Repaint debts are reported, never polled. State mutators push their owed
 * areas into the DirtyRegions the tree was wired with; the renderer asks the
 * collector which strips owe work. A widget with no sink wired (nobody called
 * set_dirty_sink) drops its reports quietly, the same stance the empty host
 * table takes at the other boundary.
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
    /// An adopted subtree inherits this widget's dirty sink, so wiring the root
    /// before or after assembly both reach every widget.
    [[nodiscard]] bool add_child(Widget* child) noexcept;
    [[nodiscard]] std::size_t child_count() const noexcept;

    /// Wires the repaint reporter for this whole subtree. Called once at tree
    /// assembly, usually on the root; late-added children inherit it too.
    void set_dirty_sink(DirtyRegions* sink) noexcept;

    /// Draws this subtree pre-order, skipping every invisible widget.
    void draw(const Painter& painter) const noexcept;

    /// Returns the widget that receives a click at this point, walking children
    /// in reverse add order; null when the point lands outside this subtree or
    /// on an invisible branch.
    [[nodiscard]] Widget* hit_test(base::PointI at) noexcept;

    /// Receives a pointer press after hit testing picked this widget. The
    /// default control takes no input, so doing nothing here is the honest base.
    virtual void on_pointer_down() noexcept {}
    /// Receives the matching release. Press and release pair per widget: a
    /// release that hits nobody leaves every press standing.
    virtual void on_pointer_up() noexcept {}

    /// Receives a key code while this widget holds the focus. The dispatcher
    /// routes every code except Tab here; a control compares against the codes
    /// events.hpp names and ignores the rest. The default control consumes no
    /// keys, so an empty body is the honest base again.
    virtual void on_key_down([[maybe_unused]] std::uint32_t code) noexcept {}

    /// Whether Tab traversal may hand this widget the keyboard focus. The
    /// honest base takes no input at all; interactive controls override.
    [[nodiscard]] virtual bool focusable() const noexcept { return false; }

    /// Whether this widget holds the keyboard focus. At most one widget per
    /// tree holds it, and advance_focus() is what keeps that invariant.
    [[nodiscard]] bool focused() const noexcept;
    void set_focused(bool on) noexcept;

    /// Returns the widget holding the focus in this subtree, null when nobody
    /// does. The dispatcher routes a key code to this holder's on_key_down.
    [[nodiscard]] Widget* focused_widget() noexcept;

    /// Moves focus to the next focusable widget in draw order, wrapping past
    /// the end. Hidden branches take no part, matching draw() and hit_test().
    /// Returns false when nothing in the subtree is focusable; focus stays.
    bool advance_focus() noexcept;

  protected:
    /// Paints this widget only; the tree walk around it belongs to draw().
    virtual void on_draw(const Painter& painter) const noexcept = 0;

    /// Reports an area as owing a repaint. State mutators call this as they
    /// change; with no sink wired the report is dropped, which keeps widgets
    /// usable in painter-only tests.
    void invalidate(base::RectI area) noexcept {
        if (dirty_ != nullptr) {
            dirty_->push(area);
        }
    }

  private:
    static Widget* find_focused_in(Widget& node) noexcept;
    static Widget* find_focusable_after(Widget& node, Widget* current, bool& seen_current) noexcept;

    base::RectI bounds_;
    bool visible_ = true;
    bool focused_ = false;
    std::array<Widget*, kMaxChildren> children_{};
    std::size_t child_count_ = 0;
    DirtyRegions* dirty_ = nullptr;
};

} // namespace visor::ui

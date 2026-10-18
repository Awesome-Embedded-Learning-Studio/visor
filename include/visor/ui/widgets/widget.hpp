#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"

namespace visor::ui {

/**
 * @brief Base class of every control: a bounding box, a visibility switch and an
 *        unbounded child list linked through sibling pointers.
 *
 * Widgets are runtime objects by spec: their state changes as events arrive, and
 * templating the whole scene would rebuild the program for every layout. What
 * stays compile-time is the raster profile below them, reached through Painter.
 *
 * Two walking orders live on one tree. draw() goes pre-order -- parent first,
 * children on top of it -- because later pixels overwrite earlier ones. hit_test()
 * walks the same tree backwards so the widget that paints last is the one that
 * receives the click. raise() and lower() move a child within its parent's
 * list, so both walks follow a new stacking order without either one changing.
 *
 * Focus lives here too: at most one widget holds the keyboard focus at a time,
 * draw() paints the one-pixel ring for the holder, and advance_focus() walks
 * draw order to hand it on when Tab arrives. That walk can be bounded to a
 * focus domain (a window subtree) without changing the default whole-tree
 * behavior.
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

    /// A process-unique identity assigned at construction. Cross-round
    /// bookkeeping uses it instead of the widget's address, so a destroyed
    /// widget's entry cannot be revived by a new widget that happens to land
    /// on the same address. 32 bits; a session would have to build four
    /// billion widgets before one repeats.
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

    [[nodiscard]] bool visible() const noexcept;
    void set_visible(bool on) noexcept;

    /// Adopts a child; returns false when child is null, already belongs to
    /// another parent, or would become this widget's own ancestor (a cycle no
    /// walk could finish). There is no capacity bound: the list is links, not
    /// storage. An adopted subtree inherits this widget's dirty sink, so wiring
    /// the root before or after assembly both reach every widget.
    [[nodiscard]] bool add_child(Widget* child) noexcept;

    /// Removes a direct child and closes the gap around it. Returns false
    /// when child is null or is not a direct child. The child is detached, not
    /// destroyed: its parent link clears, its subtree stops reporting into
    /// this tree's dirty sink, and any focus it holds is released so a later
    /// adoption cannot resurrect a stale ring. A visible child reports its own
    /// box as owing repaint, because the pixels it covered now belong to
    /// whatever is beneath it. raise() and lower() only reorder, so this is
    /// the one door a child leaves through.
    [[nodiscard]] bool remove_child(Widget* child) noexcept;

    [[nodiscard]] std::size_t child_count() const noexcept;

    /// The child at index in add order, null past the count. Costs one link hop
    /// per index, so it is a walking door for layout, not a spot query. The
    /// layout walks read their children through this door; ownership and order
    /// belong to add_child.
    [[nodiscard]] Widget* child(std::size_t index) noexcept;

    /// Moves this widget to the top of its parent's child list, where draw()
    /// paints it last and hit_test() asks it first. Returns false when the
    /// widget has no parent or already sits on top. A visible widget reports
    /// its own box as owing repaint: the overlap it now covers and the overlap
    /// it uncovers both sit inside that box, and every pixel outside it keeps
    /// the same topmost painter. A hidden widget owns no pixels, so it reports
    /// nothing; showing it later reports the box again.
    bool raise() noexcept;

    /// The mirror of raise(): moves this widget to the bottom of its parent's
    /// child list, painting it first and hit testing it last. The same return,
    /// hidden and repaint rules apply: false when there is nowhere lower to go.
    bool lower() noexcept;

    /// Replaces the bounding box and reports both boxes as owing repaint: a
    /// widget that moved or resized leaves pixels standing in the old box and
    /// claims new ones it never painted. An unchanged box reports nothing.
    /// Public since the layout story arrived (B15): a box arranging its
    /// children and a label re-measuring itself are the same right, and the
    /// reports fire either way so no move is ever silent.
    void set_bounds(base::RectI bounds) noexcept;

    /// Wires the repaint reporter for this whole subtree. Called once at tree
    /// assembly, usually on the root; late-added children inherit it too.
    void set_dirty_sink(DirtyRegions* sink) noexcept;

    /// Wires the color scheme the render loop reads from this tree's root and
    /// reports every widget's bounds as owed repaint -- switching a theme at
    /// runtime is this one call, the pixels follow the reports. The scheme is
    /// one per frame: a call on a subtree still reports repaint, but the frame
    /// uses the root's scheme. An unwired tree renders through the light
    /// preset. A same-pointer call wires nothing twice.
    void set_theme(const Theme* theme) noexcept;

    /// The scheme wired to this tree, null when nobody wired one. The render
    /// loop reads the root's scheme to build the painter; widgets themselves
    /// name roles.
    [[nodiscard]] const Theme* theme() const noexcept { return theme_; }

    /// Draws this subtree pre-order, skipping every invisible widget.
    void draw(const Painter& painter) const noexcept;

    /// Returns the widget that receives a click at this point, walking children
    /// in reverse add order; null when the point lands outside this subtree or
    /// on an invisible branch.
    [[nodiscard]] Widget* hit_test(base::PointI at) noexcept;

    /// Receives a pointer press at the position it landed (inside this
    /// widget's bounds, since hit testing routed it). Every pointer handler
    /// carries its position: in the capture world a control tracks the
    /// pointer it owns wherever it goes, and the press is where tracking
    /// starts (a slider grabs at the press). The default control takes no
    /// input, so an empty body is the honest base.
    virtual void on_pointer_down([[maybe_unused]] base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {}
    /// Receives the matching release at the position it landed. Capture routes
    /// the release to the press's owner wherever that is, so unlike the press
    /// the position is the receiver's to judge (a button calls it a click only
    /// inside its bounds). The default control takes no input.
    virtual void on_pointer_up([[maybe_unused]] base::PointI at) noexcept {}
    /// Receives a pointer movement. While a press is held the owner hears
    /// every move (drag's foundation); otherwise hit testing's topmost widget
    /// does (hover's foundation), and a move that finds nobody reaches nobody.
    /// The default control ignores movement.
    virtual void on_pointer_move([[maybe_unused]] base::PointI at) noexcept {}

    /// Receives the second release of a primary-click pair that landed on
    /// this widget inside the router's time window and position slop.
    /// modifiers are those held at the second press, the release itself
    /// carries none. The release's own on_pointer_up has already run, so this
    /// is an additional gesture, not a replacement: a control that treats
    /// every click as an action has already acted twice. The default control
    /// ignores it.
    virtual void on_double_click([[maybe_unused]] base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {}
    /// Receives a wheel word at the position it landed. Wheels route by hover,
    /// never by press ownership: a wheel while a press is held still goes to
    /// the widget under the pointer. The default control ignores the wheel.
    virtual void on_pointer_wheel([[maybe_unused]] const host::PointerWheel& wheel) noexcept {}

    /// Receives a key code while this widget holds the focus. The dispatcher
    /// routes every code except Tab here; a control compares against the codes
    /// events.hpp names and ignores the rest. The default control consumes no
    /// keys, so an empty body is the honest base again.
    virtual void on_key_down([[maybe_unused]] std::uint32_t code,
                             [[maybe_unused]] host::Modifiers modifiers) noexcept {}

    /// Receives the matching release, if the widget still holds the focus
    /// when it arrives. Release tracking is where keyboard activation
    /// completes (Button presses on down, fires on up); the default control
    /// tracks nothing, so an empty body is the honest base. Codes arrive
    /// verbatim: a release the control never pressed is its to ignore.
    virtual void on_key_up([[maybe_unused]] std::uint32_t code,
                           [[maybe_unused]] host::Modifiers modifiers) noexcept {}

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
    /// Returns false when the domain holds no visible focusable widget; focus
    /// stays where it is.
    ///
    /// @param scope The focus domain. Null (the default) or this widget means
    ///        the whole subtree rooted here, which is the historical Tab
    ///        behavior. A descendant narrows the walk to that subtree, so Tab
    ///        cycles inside one window and never crosses into a sibling. Any
    ///        other pointer -- a window that was torn down or detached -- falls
    ///        back to this subtree. The pointer is compared against live nodes
    ///        only and is never followed when it is not found, so a stale scope
    ///        cannot steer the walk into freed memory. The scope is not stored;
    ///        it is the caller's non-owning hint for the duration of the call.
    bool advance_focus(Widget* scope = nullptr) noexcept;

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
    static bool holds_widget(const Widget& node, const Widget* target) noexcept;
    static void release_focus_in(Widget& node) noexcept;

    /// Shared body of raise() and lower(): moves this widget to one end of its
    /// parent's child list and reports the repaint debt.
    bool move_to_edge(bool to_top) noexcept;

    base::RectI bounds_;
    std::uint32_t id_;
    bool visible_ = true;
    bool focused_ = false;
    // Children are an intrusive doubly linked list. The tree borrows them from
    // whoever constructed them, so there is no storage, no allocation and no
    // cap; both ends are kept because draw() walks forward and hit_test()
    // walks backward.
    Widget* first_child_ = nullptr;
    Widget* last_child_ = nullptr;
    Widget* prev_sibling_ = nullptr;
    Widget* next_sibling_ = nullptr;
    std::size_t child_count_ = 0;
    Widget* parent_ = nullptr;
    DirtyRegions* dirty_ = nullptr;
    const Theme* theme_ = nullptr;
};

} // namespace visor::ui

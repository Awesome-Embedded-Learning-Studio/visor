#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief A full-screen transparent mount for one floating subtree.
 *
 * The layer is added to the tree's root and starts hidden. open() adopts the
 * content as its only child, raises the layer above its siblings, and shows
 * it. The content positions itself in absolute canvas coordinates, so it is
 * not constrained by the geometry of the window that logically owns it; that
 * window is not its parent. Mounting on the root is what makes a menu able
 * to hang past the window edge.
 *
 * Because the layer covers the screen, hit testing outside the content stops
 * at the layer itself. Its on_pointer_down closes the popup and consumes the
 * press, so the widget underneath never hears it. Moves and wheels outside
 * the content stop at the layer too, which keeps hover and scroll behind a
 * popup quiet; inside the content, ordinary delivery applies.
 *
 * close() detaches the content and returns it, or null when nothing was
 * open. The caller keeps ownership: the layer never destroys the content.
 * The dismiss callback fires only when an outside press closed the popup,
 * not for an explicit close.
 *
 * Opening and closing toggle the layer's visibility, and the layer's bounds
 * are the whole screen, so each toggle reports the screen as owing repaint.
 * That is the first version's price: correct and never under-covering, at one
 * full-screen rebuild per open or close. A later cut can swap the toggle for
 * an input guard that reports only the content box.
 *
 * The layer itself is not focusable. A popup that must never take keyboard
 * focus should build its content from non-focusable widgets; a focusable
 * child still becomes the focus holder through the ordinary press path.
 */
class PopupLayer final : public Widget {
  public:
    /// Called after an outside press detached the content. The content is
    /// still alive and caller-owned at the call.
    using DismissHandler = void (*)(PopupLayer& self, Widget& content, void* context);

    explicit PopupLayer(base::RectI bounds) noexcept;

    /// Adopts content and shows the layer. False when content is null, this
    /// layer, already open, or already owned by another parent. The layer
    /// raises itself, so it paints and hits above the root's other children.
    [[nodiscard]] bool open(Widget* content) noexcept;

    /// Detaches and returns the content, or null when nothing was open.
    [[nodiscard]] Widget* close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return content_ != nullptr; }
    [[nodiscard]] Widget* content() const noexcept { return content_; }

    void set_on_dismiss(DismissHandler handler, void* context = nullptr) noexcept;

    /// An outside press lands on the layer itself (the content and its
    /// children were asked first), so this closes and consumes it.
    void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw([[maybe_unused]] const Painter& painter) const noexcept override {}
    // The layer paints nothing; the content child carries every pixel.

  private:
    Widget* content_ = nullptr;
    DismissHandler on_dismiss_ = nullptr;
    void* dismiss_context_ = nullptr;
};

} // namespace visor::ui

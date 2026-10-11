#pragma once

#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief Delivers one event to the tree. Declared here, defined in the source:
 *        the dispatcher knows every event word, the widgets know none of them.
 *
 * Pointer presses and releases go to whoever hit testing picks; the point that
 * finds no widget delivers to nobody. Keyboard and movement events have no
 * consumer until focus and gestures arrive, so they end the round unconsumed
 * beyond the count.
 */
void dispatch_event(Widget& root, const host::Event& event) noexcept;

/**
 * @brief One round of the spec's loop, full-frame simplification: pump every
 *        waiting event into the tree, repaint the whole frame, submit it.
 *
 * The dirty-region collection and the strip-wise rebuild are later batches;
 * until they land this paints everything each round, which stays correct by
 * construction. Templates on the canvas type like every draw-side entry point:
 * the run loop owns the canvas/painter binding, widgets never do.
 *
 * @return Events consumed this round, drained events included even when no
 *         widget wanted them.
 */
template <typename CanvasT>
[[nodiscard]] int run_once(const host::HostTable& host, Widget& root, CanvasT& frame) noexcept {
    int consumed = 0;
    host::Event event;
    while (host::try_poll_event(host, event)) {
        ++consumed;
        dispatch_event(root, event);
    }
    const CanvasPainter<CanvasT> painter{frame};
    root.draw(painter);
    // A table without a flush entry submits nothing and kills nobody, the same
    // stance try_poll_event takes toward a missing poll entry; callers that
    // care check the table once at setup, not every round.
    static_cast<void>(host::submit_frame(host, frame));
    return consumed;
}

} // namespace visor::ui

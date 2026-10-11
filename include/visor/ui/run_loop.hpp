#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief Delivers one event to the tree. Declared here, defined in the source:
 *        the dispatcher knows every event word, the widgets know none of them.
 *
 * Pointer presses and releases go to whoever hit testing picks; the point that
 * finds no widget delivers to nobody. Tab (the one named key code) moves the
 * focus along the tree; every other key code and the pointer movement end the
 * round unconsumed beyond the count until their consumers arrive.
 */
void dispatch_event(Widget& root, const host::Event& event) noexcept;

/**
 * @brief Draws the whole tree through one scene window into a buffer that
 *        stores exactly that window, then submits the window to the host.
 *
 * The one render primitive everything else composes: a full frame is the
 * window that covers the canvas, a strip rebuild is the same tree replayed
 * under each strip's window, a dirty repaint is the windows the collector
 * names. The replay discipline from the widget chapter is what makes this
 * legal: drawing reads state and touches nothing, so the same tree redrawn
 * under every window lands the same pixels every time. An empty table
 * submits nothing and kills nobody.
 *
 * @param window The scene rectangle this buffer stores; its top-left corner
 *        is buffer pixel (0, 0). May be smaller than the buffer (tail strip).
 */
template <typename CanvasT>
void render_window(const Widget& root, CanvasT& buffer, base::RectI window,
                   const host::HostTable& host) noexcept {
    const CanvasPainter<CanvasT> painter{buffer, window};
    root.draw(painter);
    static_cast<void>(
        host::submit_region(host, window, buffer.pixels().data(), CanvasT::kStrideBytes));
}

/**
 * @brief Rebuilds the whole screen one strip at a time. The compile-time strip
 *        canvas is the raster profile; the runtime screen size only decides
 *        how many strips run and how the tail one clips, which is the shape
 *        the buffer budget buys. Every strip replays the whole tree.
 */
template <typename CanvasT>
void render_strips(const Widget& root, CanvasT& strip, base::SizeI screen,
                   const host::HostTable& host) noexcept {
    const int strip_h = static_cast<int>(CanvasT::kHeight);
    for (int top = 0; top < screen.h; top += strip_h) {
        const int rows = (screen.h - top < strip_h) ? (screen.h - top) : strip_h;
        render_window(
            root, strip,
            base::RectI{.pos = base::PointI{0, top}, .size = base::SizeI{.w = screen.w, .h = rows}},
            host);
    }
}

/**
 * @brief Rebuilds only the strips the collector says owe work. A strip nobody
 *        pushed against keeps its old pixels on the host; the collapsed box of
 *        an overflowing list touches every strip it spans, so a degraded list
 *        costs extra rebuilds and never misses one.
 */
template <typename CanvasT>
void render_strips(const Widget& root, CanvasT& strip, base::SizeI screen,
                   const host::HostTable& host, const DirtyRegions& dirty) noexcept {
    const int strip_h = static_cast<int>(CanvasT::kHeight);
    for (int top = 0; top < screen.h; top += strip_h) {
        const int rows = (screen.h - top < strip_h) ? (screen.h - top) : strip_h;
        const base::RectI strip_window{.pos = base::PointI{0, top},
                                       .size = base::SizeI{.w = screen.w, .h = rows}};
        if (dirty.touches(strip_window)) {
            render_window(root, strip, strip_window, host);
        }
    }
}

/**
 * @brief One round of the spec's loop, full-frame simplification: pump every
 *        waiting event into the tree, repaint the whole frame, submit it.
 *
 * Full-frame repaint stays correct by construction; the strip and dirty paths
 * above are the same primitive wearing smaller windows, ready when a batch
 * needs them. Templates on the canvas type like every draw-side entry point:
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
    const base::RectI full{.pos = base::PointI{0, 0},
                           .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                               .h = static_cast<int>(CanvasT::kHeight)}};
    render_window(root, frame, full, host);
    return consumed;
}

} // namespace visor::ui

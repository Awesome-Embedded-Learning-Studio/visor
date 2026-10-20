#pragma once

#include <variant>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

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
    const Theme& scheme = root.theme() != nullptr ? *root.theme() : kLightTheme;
    const CanvasPainter<CanvasT> painter{buffer, window, scheme};
    root.draw(painter);
    static_cast<void>(host.submit_region(window, buffer.pixels().data(), CanvasT::kStrideBytes,
                                         host::kWireFormat<typename CanvasT::Traits>));
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
        const base::RectI strip_window{
            .pos = base::PointI{0, top},
            .size = base::SizeI{.w = screen.w, .h = rows},
        };
        if (dirty.touches(strip_window)) {
            render_window(root, strip, strip_window, host);
        }
    }
}

/**
 * @brief One round of the spec's full pipeline: pump events, rebuild the owed
 *        strips, retire the debts.
 *
 * The economy path beside run_once: a round whose events changed nothing owes
 * nothing and submits nothing, which is the whole point of the reports. The
 * first frame is the caller's job -- paint it with render_window or
 * render_strips, or push the whole screen as dirty -- because this loop only
 * ever repaints what a report named. The collector clears after the rebuild:
 * the owed pixels are on the host, nothing stays owed into the next round.
 *
 * The rhythm rule: a round consumes at most one key event (a KeyDown or a
 * KeyUp), leaving the rest queued for the rounds that follow; pointer events
 * drain freely. A key press and its release can therefore never share a
 * round, so the pressed state a control shows between them owns at least one
 * full rebuild -- the timing story is structural, borrowed from neither the
 * host's repeat behavior nor a clock. Pointer streams pay no such toll:
 * their events are samples of position, not beats of time.
 *
 * @param[in] input The router carrying pointer press ownership; caller-owned
 *                  state beside the tree, the same stance as the collector.
 *
 * @return Events consumed this round, drained events included even when no
 *         widget wanted them.
 */
template <typename CanvasT>
[[nodiscard]] int run_dirty(const host::HostTable& host, Widget& root, InputRouter& input,
                            CanvasT& strip, base::SizeI screen, DirtyRegions& dirty) noexcept {
    int consumed = 0;
    const std::uint64_t now_ms = host.now_ms();
    host::Event event;
    while (host.poll(event)) {
        ++consumed;
        dispatch_event(root, input, event, now_ms, host.has_clock());
        if (std::holds_alternative<host::KeyDown>(event) ||
            std::holds_alternative<host::KeyUp>(event)) {
            break; // one key event per round: the rhythm rule above
        }
    }
    render_strips(root, strip, screen, host, dirty);
    dirty.clear();
    return consumed;
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
 * The same rhythm rule as run_dirty applies: at most one key event per
 * round, pointer events drained freely, so a press and its release each own
 * a frame wherever the loop runs.
 *
 * @return Events consumed this round, drained events included even when no
 *         widget wanted them.
 */
template <typename CanvasT>
[[nodiscard]] int run_once(const host::HostTable& host, Widget& root, InputRouter& input,
                           CanvasT& frame) noexcept {
    int consumed = 0;
    const std::uint64_t now_ms = host.now_ms();
    host::Event event;
    while (host.poll(event)) {
        ++consumed;
        dispatch_event(root, input, event, now_ms, host.has_clock());
        if (std::holds_alternative<host::KeyDown>(event) ||
            std::holds_alternative<host::KeyUp>(event)) {
            break; // one key event per round: the rhythm rule
        }
    }
    const base::RectI full{
        .pos = base::PointI{0, 0},
        .size =
            base::SizeI{
                .w = static_cast<int>(CanvasT::kWidth),
                .h = static_cast<int>(CanvasT::kHeight),
            },
    };
    render_window(root, frame, full, host);
    return consumed;
}

} // namespace visor::ui

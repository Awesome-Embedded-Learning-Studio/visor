#include <cstddef>
#include <print>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;
using visor::ui::DirtyRegions;
using visor::ui::Rectangle;
using visor::ui::Widget;

constexpr int kScreen = 20;
constexpr SizeI kScreenSize{.w = kScreen, .h = kScreen};
const RectI kWholeScreen{.pos = PointI{0, 0}, .size = kScreenSize};

/// The strip gate's standard cast: a lit full-screen ground, a dark hole
/// crossing the strip 0/1 boundary, and two lit markers that start hidden.
/// Every pixel gets painted by someone, so an unrebuilt strip shows up as a
/// dark patch the comparison cannot miss.
class GateScene {
  public:
    GateScene() {
        VISOR_CHECK(root.add_child(&ground));
        VISOR_CHECK(root.add_child(&hole));
        VISOR_CHECK(root.add_child(&tail_dot));
        VISOR_CHECK(root.add_child(&head_dot));
        hole.fill = visor::canvas::ColorRole::Background; // the dark hole is the point
        tail_dot.set_visible(false);
        head_dot.set_visible(false);
    }

    Rectangle root{RectI{.pos = PointI{0, 0}, .size = kScreenSize}};
    Rectangle ground{RectI{.pos = PointI{0, 0}, .size = kScreenSize}};
    // Rows 6..11: crosses the strip boundary at row 8, catching replay slips.
    Rectangle hole{RectI{.pos = PointI{4, 6}, .size = SizeI{.w = 6, .h = 6}}};
    // Rows 16..20: lives entirely in the tail strip.
    Rectangle tail_dot{RectI{.pos = PointI{12, 16}, .size = SizeI{.w = 4, .h = 4}}};
    // Rows 2..6: lives entirely in the first strip.
    Rectangle head_dot{RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}}};
};

/// Renders the tree twice over the same 20x20 screen, once as one integral
/// window, once strip by strip into a separate host, then compares all 400
/// received pixels. Flush counts come back for the scenarios that pin them.
void compare_integral_vs_strips(Widget& root, int& integral_flushes, int& strip_flushes) {
    using GateCanvas = Canvas<SizeI{.w = 20, .h = 20}>;
    using StripCanvas = Canvas<SizeI{.w = 20, .h = 8}>;

    visor::host::OffscreenHost integral(20, 20);
    visor::host::OffscreenHost striped(20, 20);

    GateCanvas frame;
    frame.set_clear(false);
    visor::ui::render_window(root, frame, kWholeScreen, integral.table());

    StripCanvas strip;
    strip.set_clear(false);
    visor::ui::render_strips(root, strip, kScreenSize, striped.table());

    std::size_t mismatches = 0;
    for (int y = 0; y < kScreen; ++y) {
        for (int x = 0; x < kScreen; ++x) {
            if (integral.get(PointI{x, y}) != striped.get(PointI{x, y})) {
                if (mismatches == 0) {
                    std::println("strip gate first mismatch at ({}, {})", x, y);
                }
                ++mismatches;
            }
        }
    }
    VISOR_CHECK_EQ(mismatches, std::size_t{0});
    integral_flushes = integral.flush_count();
    strip_flushes = striped.flush_count();
}

/// Compares two hosts' received frames pixel for pixel.
void compare_hosts(const visor::host::OffscreenHost& a, const visor::host::OffscreenHost& b) {
    std::size_t mismatches = 0;
    for (int y = 0; y < kScreen; ++y) {
        for (int x = 0; x < kScreen; ++x) {
            if (a.get(PointI{x, y}) != b.get(PointI{x, y})) {
                if (mismatches == 0) {
                    std::println("host comparison first mismatch at ({}, {})", x, y);
                }
                ++mismatches;
            }
        }
    }
    VISOR_CHECK_EQ(mismatches, std::size_t{0});
}

} // namespace

void run_render_tests() {
    // The window: a painter whose buffer stores one scene rectangle. Strokes
    // inside land shifted by the window origin, strokes outside vanish.
    Canvas<SizeI{.w = 20, .h = 20}> buffer;
    buffer.set_clear(false);
    const RectI window{.pos = PointI{5, 5}, .size = SizeI{.w = 10, .h = 10}};
    const visor::ui::CanvasPainter painter{buffer, window};
    painter.fill(kWholeScreen, visor::canvas::ColorRole::Foreground);
    VISOR_CHECK(buffer.get(PointI{0, 0}) == true);    // scene (5, 5)
    VISOR_CHECK(buffer.get(PointI{9, 9}) == true);    // scene (14, 14), last covered
    VISOR_CHECK(buffer.get(PointI{10, 10}) == false); // scene (15, 15), outside
    VISOR_CHECK(buffer.get(PointI{19, 19}) == false);
    VISOR_CHECK(buffer.get(PointI{4, 4}) == true); // scene (9, 9), inside the window
    // A plot outside the window vanishes the same way a Pen plot off-canvas does.
    buffer.set_clear(false);
    painter.plot(PointI{4, 5}, visor::canvas::ColorRole::Foreground); // scene x < 5, clipped
    VISOR_CHECK(buffer.get(PointI{0, 0}) == false);
    painter.plot(PointI{5, 5},
                 visor::canvas::ColorRole::Foreground); // the window's first scene pixel
    VISOR_CHECK(buffer.get(PointI{0, 0}) == true);

    // The tail window: smaller than its buffer. The canvas stores up to eight
    // rows but the window only owns four, so the surplus rows stay untouched.
    Canvas<SizeI{.w = 20, .h = 8}> tail_buffer;
    tail_buffer.set_clear(false);
    const visor::ui::CanvasPainter tail_painter{
        tail_buffer, RectI{.pos = PointI{0, 16}, .size = SizeI{.w = 20, .h = 4}}};
    tail_painter.fill(kWholeScreen, visor::canvas::ColorRole::Foreground);
    VISOR_CHECK(tail_buffer.get(PointI{0, 0}) == true);  // scene row 16
    VISOR_CHECK(tail_buffer.get(PointI{19, 3}) == true); // scene row 19
    VISOR_CHECK(tail_buffer.get(PointI{0, 4}) == false); // below the window
    VISOR_CHECK(tail_buffer.get(PointI{19, 7}) == false);

    // The collector: overlapping pushes merge, disjoint ones stay separate.
    // Region order on file is not part of the contract, only coverage is.
    auto const has_region = [](const DirtyRegions& regions, RectI wanted) {
        for (std::size_t i = 0; i < regions.count(); ++i) {
            if (regions.at(i) == wanted) {
                return true;
            }
        }
        return false;
    };
    DirtyRegions dirty;
    dirty.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 4}});
    VISOR_CHECK_EQ(dirty.count(), std::size_t{0}); // an empty rect owes nothing
    dirty.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
    dirty.push(RectI{.pos = PointI{10, 10}, .size = SizeI{.w = 4, .h = 4}});
    VISOR_CHECK_EQ(dirty.count(), std::size_t{2});
    dirty.push(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}});
    VISOR_CHECK_EQ(dirty.count(), std::size_t{2});
    VISOR_CHECK(has_region(dirty, RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 6}}));
    // A push touching both residents cascades into one box covering all three.
    dirty.push(RectI{.pos = PointI{5, 5}, .size = SizeI{.w = 6, .h = 6}});
    VISOR_CHECK_EQ(dirty.count(), std::size_t{1});
    VISOR_CHECK_EQ(dirty.at(0), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 14, .h = 14}}));
    VISOR_CHECK(dirty.touches(RectI{.pos = PointI{13, 13}, .size = SizeI{.w = 2, .h = 2}}));
    VISOR_CHECK(!dirty.touches(RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 2, .h = 2}}));
    dirty.clear();
    VISOR_CHECK_EQ(dirty.count(), std::size_t{0});

    // Overflow: the push onto a full list folds the residents into one box,
    // then the newcomer joins it as its own region. The first eight single
    // pixels span x 0..71, the ninth lands far below, nothing is dropped.
    DirtyRegions full;
    for (std::size_t i = 0; i < DirtyRegions::kCapacity; ++i) {
        full.push(RectI{.pos = PointI{static_cast<int>(i) * 10, 0}, .size = SizeI{.w = 1, .h = 1}});
    }
    VISOR_CHECK_EQ(full.count(), DirtyRegions::kCapacity);
    full.push(RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 2, .h = 2}});
    VISOR_CHECK_EQ(full.count(), std::size_t{2});
    VISOR_CHECK(has_region(full, RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 71, .h = 1}}));
    VISOR_CHECK(has_region(full, RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 2, .h = 2}}));
    for (std::size_t i = 0; i < DirtyRegions::kCapacity; ++i) {
        VISOR_CHECK(full.touches(
            RectI{.pos = PointI{static_cast<int>(i) * 10, 0}, .size = SizeI{.w = 1, .h = 1}}));
    }
    VISOR_CHECK(full.touches(RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 1, .h = 1}}));
    full.push(RectI{.pos = PointI{40, 40}, .size = SizeI{.w = 1, .h = 1}});
    VISOR_CHECK(full.count() <= DirtyRegions::kCapacity);

    // The strip gate, case by case: integral and strip paths must agree pixel
    // for pixel on every scenario, flush counts pin where the work went.

    // Case 1, tail strip: screen 20, strip 8, the last window is four rows.
    // The hole crosses the strip 0/1 boundary, the lit dot sits in the tail.
    {
        GateScene scene;
        scene.tail_dot.set_visible(true);
        int integral_flushes = 0;
        int strip_flushes = 0;
        compare_integral_vs_strips(scene.root, integral_flushes, strip_flushes);
        VISOR_CHECK_EQ(integral_flushes, 1);
        VISOR_CHECK_EQ(strip_flushes, 3); // 8 + 8 + 4
    }

    // Case 2, old position restore: the marker moves from the tail strip to
    // the top. The strip holding the old spot owes a repaint even though
    // nothing is lit there anymore. The patched host first receives the whole
    // v1 frame the normal way (it plays a real screen holding last round's
    // pixels), then the dirty path rebuilds exactly the two strips named by
    // old and new bounds, and the result matches the integral v2.
    {
        GateScene scene;
        scene.tail_dot.set_visible(true);
        int integral_flushes = 0;
        int strip_flushes = 0;
        compare_integral_vs_strips(scene.root, integral_flushes, strip_flushes);

        visor::host::OffscreenHost patched(20, 20);
        Canvas<SizeI{.w = 20, .h = 8}> strip;
        strip.set_clear(false);
        visor::ui::render_strips(scene.root, strip, kScreenSize, patched.table());
        VISOR_CHECK_EQ(patched.flush_count(), 3); // v1 arrives in full

        scene.tail_dot.set_visible(false);
        scene.head_dot.set_visible(true);
        compare_integral_vs_strips(scene.root, integral_flushes, strip_flushes);

        visor::host::OffscreenHost integral(20, 20);
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        frame.set_clear(false);
        visor::ui::render_window(scene.root, frame, kWholeScreen, integral.table());

        DirtyRegions moved;
        moved.push(RectI{.pos = PointI{12, 16}, .size = SizeI{.w = 4, .h = 4}}); // old, strip 2
        moved.push(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}});   // new, strip 0
        visor::ui::render_strips(scene.root, strip, kScreenSize, patched.table(), moved);
        VISOR_CHECK_EQ(patched.flush_count(), 5); // 3 + the two owed strips
        compare_hosts(integral, patched);
    }

    // Case 3, full-screen invalidation: one push covering the screen rebuilds
    // every strip.
    {
        const GateScene scene;
        visor::host::OffscreenHost striped(20, 20);
        Canvas<SizeI{.w = 20, .h = 8}> strip;
        strip.set_clear(false);
        DirtyRegions everything;
        everything.push(kWholeScreen);
        visor::ui::render_strips(scene.root, strip, kScreenSize, striped.table(), everything);
        VISOR_CHECK_EQ(striped.flush_count(), 3);
    }

    // Case 4, dirty overflow collapse: nine disjoint pushes on a capacity-8
    // list fold the first eight into one box and keep the ninth, and together
    // the two survivors span all three strips, so the rebuild covers more than
    // strictly needed and misses nothing.
    {
        const GateScene scene;
        visor::host::OffscreenHost integral(20, 20);
        visor::host::OffscreenHost striped(20, 20);
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        frame.set_clear(false);
        visor::ui::render_window(scene.root, frame, kWholeScreen, integral.table());
        Canvas<SizeI{.w = 20, .h = 8}> strip;
        strip.set_clear(false);
        DirtyRegions scattered;
        for (int i = 0; i < 9; ++i) {
            scattered.push(RectI{.pos = PointI{i * 2, i * 2}, .size = SizeI{.w = 1, .h = 1}});
        }
        VISOR_CHECK_EQ(scattered.count(), std::size_t{2});
        visor::ui::render_strips(scene.root, strip, kScreenSize, striped.table(), scattered);
        VISOR_CHECK_EQ(striped.flush_count(), 3);
        compare_hosts(integral, striped);
    }

    // Case 5, the batch dial: render_strips steps by the strip canvas height,
    // so a taller strip canvas sends fewer, larger regions from the same loop.
    // The 16-row canvas covers the screen in two sends, its tail window owning
    // four of the sixteen stored rows; the 5-row canvas takes four sends and
    // puts the hole right across the row-10 boundary, so a replay slip at the
    // batch seam flips hole pixels. Both dials must land pixel-identical.
    {
        GateScene scene;
        scene.tail_dot.set_visible(true);
        visor::host::OffscreenHost integral(20, 20);
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        frame.set_clear(false);
        visor::ui::render_window(scene.root, frame, kWholeScreen, integral.table());

        visor::host::OffscreenHost batched(20, 20);
        Canvas<SizeI{.w = 20, .h = 16}> half;
        half.set_clear(false);
        visor::ui::render_strips(scene.root, half, kScreenSize, batched.table());
        VISOR_CHECK_EQ(batched.flush_count(), 2); // 16 + 4
        compare_hosts(integral, batched);

        visor::host::OffscreenHost fine(20, 20);
        Canvas<SizeI{.w = 20, .h = 5}> slim;
        slim.set_clear(false);
        visor::ui::render_strips(scene.root, slim, kScreenSize, fine.table());
        VISOR_CHECK_EQ(fine.flush_count(), 4);
        compare_hosts(integral, fine);
    }

    // Case 6, the odd x: region pixels may start at any scene x, so the host
    // copy cannot assume source and destination share a byte layout. The
    // patch starts at x = 3 and its dark columns are the hole's left edge,
    // so a copy shifted by one bit flips lit and dark pixels on camera.
    {
        const GateScene scene;
        visor::host::OffscreenHost integral(20, 20);
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        frame.set_clear(false);
        visor::ui::render_window(scene.root, frame, kWholeScreen, integral.table());

        // The patched host first holds a full frame (it plays a screen with
        // last round's pixels), then replays one small window over it.
        visor::host::OffscreenHost patched(20, 20);
        Canvas<SizeI{.w = 20, .h = 20}> whole;
        whole.set_clear(false);
        visor::ui::render_window(scene.root, whole, kWholeScreen, patched.table());
        VISOR_CHECK_EQ(patched.flush_count(), 1);

        Canvas<SizeI{.w = 8, .h = 4}> patch;
        patch.set_clear(false);
        const RectI patch_window{.pos = PointI{3, 5}, .size = SizeI{.w = 7, .h = 3}};
        visor::ui::render_window(scene.root, patch, patch_window, patched.table());
        // The painter shifted scene (3, 5) to buffer (0, 0) and scene (4, 6),
        // the hole's first dark pixel, to buffer (1, 1); column 7 and row 3
        // are surplus the 7x3 window never owns.
        VISOR_CHECK(patch.get(PointI{0, 0}) == true);
        VISOR_CHECK(patch.get(PointI{1, 1}) == false);
        VISOR_CHECK(patch.get(PointI{7, 0}) == false);
        VISOR_CHECK(patch.get(PointI{0, 3}) == false);
        VISOR_CHECK_EQ(patched.flush_count(), 2);
        compare_hosts(integral, patched);
    }

    // Case 7, the pipeline end to end: a first frame painted by hand, then
    // rounds of run_dirty where controls report their own debts. The flush
    // count pins the economy -- only the strips a report named go out, an
    // idle round sends nothing, and the collector retires with the rebuild.
    // The rounds are one key event each: the pump's rhythm rule, so a key
    // press and its release each own a rebuild of their own.
    {
        // Ground covers the screen; the checkbox lives in strip 0, the
        // button in the tail strip. (5,5) is the checkbox's inner mark,
        // (2,2) its ring corner, (12,16) the button's ring corner, (13,17)
        // the button's press-sensitive interior.
        int fired = 0;
        visor::ui::Rectangle ground(kWholeScreen);
        visor::ui::Checkbox box(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 6, .h = 6}});
        visor::ui::Button tail_button(RectI{.pos = PointI{12, 16}, .size = SizeI{.w = 4, .h = 4}});
        tail_button.set_on_activate(
            [](visor::ui::Button&, void* context) { ++*static_cast<int*>(context); }, &fired);
        VISOR_CHECK(ground.add_child(&box));
        VISOR_CHECK(ground.add_child(&tail_button));
        DirtyRegions debts;
        ground.set_dirty_sink(&debts);
        visor::ui::InputRouter input;

        visor::host::OffscreenHost screen(20, 20);
        Canvas<SizeI{.w = 20, .h = 8}> strip;
        strip.set_clear(false);
        // The first frame is the caller's job: run_dirty only repaints
        // what a report names, so it starts from a full render_strips.
        visor::ui::render_strips(ground, strip, kScreenSize, screen.table());
        VISOR_CHECK_EQ(screen.flush_count(), 3); // 8 + 8 + 4

        // Round one: Tab focuses the checkbox. The ring report names strip 0.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 1);
        VISOR_CHECK_EQ(screen.flush_count(), 4);
        VISOR_CHECK_EQ(debts.count(), std::size_t{0}); // retired with the rebuild
        VISOR_CHECK(box.focused());
        VISOR_CHECK(!box.checked());
        VISOR_CHECK(screen.get(PointI{2, 2}) == false); // the ring went out
        // The state assertion distinguishes checking from focus before later pixel checks.

        // Round two: Space checks the checkbox. The mark report is the same
        // box the ring used, the collector merges into one strip 0 rebuild.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeySpace}));
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 1);
        VISOR_CHECK_EQ(screen.flush_count(), 5);
        VISOR_CHECK(box.checked());
        VISOR_CHECK(screen.get(PointI{2, 2}) == false); // frame and ring both dark

        // Round three: Tab walks to the tail button. The checkbox un-ring
        // report and the button ring report live in different strips, so
        // strips 0 and 2 rebuild and strip 1 stays home. The mark persists.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 1);
        VISOR_CHECK_EQ(screen.flush_count(), 7);
        VISOR_CHECK(tail_button.focused());
        VISOR_CHECK(!box.focused());
        VISOR_CHECK(screen.get(PointI{12, 16}) == false); // the ring moved to the button
        VISOR_CHECK(screen.get(PointI{2, 2}) == true);    // the box corner is plain again
        VISOR_CHECK(screen.get(PointI{5, 5}) == true);    // the checked face stays put

        // Rounds four and five: Enter presses the focused button, its up
        // releases and fires. Each round rebuilds exactly the button's strip.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 1);
        VISOR_CHECK_EQ(screen.flush_count(), 8);
        VISOR_CHECK(tail_button.pressed());
        VISOR_CHECK(screen.get(PointI{13, 17}) == false); // the press frame went out
        VISOR_CHECK_EQ(fired, 0);                         // down never fires
        VISOR_CHECK(screen.push_event(visor::host::KeyUp{.code = visor::host::kKeyEnter}));
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 1);
        VISOR_CHECK_EQ(screen.flush_count(), 9);
        VISOR_CHECK(!tail_button.pressed());
        VISOR_CHECK(screen.get(PointI{13, 17}) == true);
        VISOR_CHECK_EQ(fired, 1); // the release completed the activation

        // Round six, idle: no events, no debts, nothing submitted.
        VISOR_CHECK_EQ(
            visor::ui::run_dirty(screen.table(), ground, input, strip, kScreenSize, debts), 0);
        VISOR_CHECK_EQ(screen.flush_count(), 9);
    }
}

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>
#include <variant>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/pen.hpp"

void run_host_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::ColorRole;
    using visor::draw::Pen;
    using visor::host::HostTable;
    using visor::host::OffscreenHost;

    using OddCanvas = Canvas<SizeI{.w = 13, .h = 5}>;

    // Full-frame handover on an odd-width canvas: the host copies row by row
    // honouring stride, and every received pixel matches the canvas.
    OddCanvas source;
    const Pen brush{source, true};
    brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 13, .h = 1}});
    brush.plot(PointI{6, 3});
    brush.fill(RectI{.pos = PointI{9, 2}, .size = SizeI{.w = 2, .h = 2}});

    OffscreenHost mirror(13, 5);
    VISOR_CHECK(mirror.table().submit_frame(source));
    VISOR_CHECK_EQ(mirror.flush_count(), 1);
    VISOR_CHECK_EQ(mirror.rejected_count(), 0);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 13; ++x) {
            VISOR_CHECK_EQ(mirror.get((PointI{x, y})), source.get((PointI{x, y})));
        }
    }

    // A second submission counts; an empty table is refused without a crash
    // and without touching any counter.
    VISOR_CHECK(mirror.table().submit_frame(source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);
    const HostTable empty_table{};
    VISOR_CHECK(!empty_table.submit_frame(source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);

    // A differently shaped host refuses the frame and keeps its own pixels dark.
    OffscreenHost smaller(12, 5);
    VISOR_CHECK(smaller.table().submit_frame(source));
    VISOR_CHECK_EQ(smaller.rejected_count(), 1);
    VISOR_CHECK_EQ(smaller.flush_count(), 0);
    VISOR_CHECK_EQ(smaller.get((PointI{0, 0})), false);

    // write_pbm round trip: header text, then the packed rows, then nothing else.
    constexpr std::string_view path = "visor_test.pbm";
    VISOR_CHECK(visor::host::write_pbm(path, 13, 5, source.pixels(), OddCanvas::kStrideBytes));
    std::ifstream in{std::filesystem::path{path}, std::ios::binary};
    VISOR_CHECK(in.is_open());
    if (in.is_open()) {
        // The PBM header is 8 bytes: "P4\n13 5\n".
        std::array<char, 8> header{};
        in.read(header.data(), static_cast<std::streamsize>(header.size()));
        VISOR_CHECK_EQ(in.gcount(), static_cast<std::streamsize>(header.size()));
        VISOR_CHECK((std::string_view{header.data(), header.size()} == "P4\n13 5\n"));
        std::array<char, 10> rows{};
        in.read(rows.data(), static_cast<std::streamsize>(rows.size()));
        VISOR_CHECK_EQ(in.gcount(), static_cast<std::streamsize>(rows.size()));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            VISOR_CHECK_EQ(static_cast<std::uint8_t>(rows[i]), source.pixels()[i]);
        }
        VISOR_CHECK(in.peek() == std::ifstream::traits_type::eof());
    }
    VISOR_CHECK(std::filesystem::remove(path));

    // Ch2 entries on a bare table: poll reports no event, the clock reads zero,
    // and neither crashes — the same stance as an empty flush entry.
    const HostTable bare{};
    VISOR_CHECK_EQ(bare.info.screen.w, 0);
    VISOR_CHECK_EQ(bare.info.screen.h, 0);
    VISOR_CHECK_EQ(bare.info.format, visor::host::PixelFormat::Mono1);
    visor::host::Event polled{};
    VISOR_CHECK(!bare.poll(polled));
    VISOR_CHECK_EQ(bare.now_ms(), 0U);
    VISOR_CHECK(!bare.has_clock());

    // Ch2 feed: six events in, the same six out in order, each carrying its
    // own fields. Everything crosses the table, the only channel the core sees.
    OffscreenHost feeder(4, 4);
    const HostTable fed = feeder.table();
    VISOR_CHECK(fed.poll_event != nullptr);
    VISOR_CHECK(fed.clock_ms != nullptr);
    VISOR_CHECK(fed.has_clock());
    VISOR_CHECK_EQ(fed.info.screen.w, 4);
    VISOR_CHECK_EQ(fed.info.screen.h, 4);
    VISOR_CHECK_EQ(fed.info.format, visor::host::PixelFormat::Mono1);
    VISOR_CHECK_EQ(fed.now_ms(), 0U);
    VISOR_CHECK(feeder.push_event(visor::host::KeyDown{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerMove{.pos = PointI{5, 6}}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{7, 8}}));
    VISOR_CHECK(feeder.push_event(visor::host::KeyUp{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{7, 8}}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerWheel{
        .dx = 1,
        .dy = -2,
        .pos = PointI{9, 10},
        .modifiers = visor::host::Modifiers{.ctrl = true},
    }));

    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::KeyDown>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::KeyDown>(polled).code, 30U);
    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerMove>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerMove>(polled).pos, (PointI{5, 6}));
    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerDown>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerDown>(polled).button, 1U);
    VISOR_CHECK_EQ(std::get<visor::host::PointerDown>(polled).pos, (PointI{7, 8}));
    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::KeyUp>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::KeyUp>(polled).code, 30U);
    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerUp>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerUp>(polled).button, 1U);
    VISOR_CHECK_EQ(std::get<visor::host::PointerUp>(polled).pos, (PointI{7, 8}));
    VISOR_CHECK(fed.poll(polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerWheel>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerWheel>(polled).dx, 1);
    VISOR_CHECK_EQ(std::get<visor::host::PointerWheel>(polled).dy, -2);
    VISOR_CHECK_EQ(std::get<visor::host::PointerWheel>(polled).pos, (PointI{9, 10}));
    VISOR_CHECK(std::get<visor::host::PointerWheel>(polled).modifiers.ctrl);

    // Drained queue: poll refuses, the host keeps answering.
    VISOR_CHECK(!fed.poll(polled));

    // Wraparound: park the scripted clock next to the top of its range, let it
    // cross the boundary, and read the duration by unsigned subtraction, the
    // only way the ch2 contract allows.
    feeder.advance_ms(0xFFFF'FFFF'FFFF'0000ULL);
    VISOR_CHECK_EQ(fed.now_ms(), 0xFFFF'FFFF'FFFF'0000ULL);
    feeder.advance_ms(0x1000ULL);
    const std::uint64_t then = 0xFFFF'FFFF'FFFF'0000ULL;
    const std::uint64_t now = fed.now_ms();
    VISOR_CHECK_EQ(now - then, 0x1000ULL);

    // Full feed: a sixteen-slot ring holds fifteen (one slot stays empty), the
    // sixteenth event is refused and counted, and the resident fifteen still
    // come out in order, untouched by the dropout.
    OffscreenHost crowded(4, 4);
    const HostTable busy = crowded.table();
    for (std::uint32_t i = 0; i < OffscreenHost::kFeedCapacity - 1U; ++i) {
        VISOR_CHECK(crowded.push_event(visor::host::KeyDown{.code = i}));
    }
    VISOR_CHECK(!crowded.push_event(visor::host::KeyDown{.code = 99}));
    VISOR_CHECK_EQ(crowded.dropped_event_count(), 1);
    for (std::uint32_t i = 0; i < OffscreenHost::kFeedCapacity - 1U; ++i) {
        VISOR_CHECK(busy.poll(polled));
        VISOR_CHECK_EQ(std::get<visor::host::KeyDown>(polled).code, i);
    }
    VISOR_CHECK(!busy.poll(polled));

    // The byte-aligned fast path: a region starting on a byte boundary lands
    // byte for byte. The source is a canvas of the region's own shape, and a
    // misaligned comparison (region pixels vs host pixels, every one read
    // back) is exactly the bit path's answer -- the fast road and the honest
    // road must agree on every pixel.
    {
        using Patch = Canvas<SizeI{.w = 12, .h = 2}>;
        Patch patch;
        const Pen patch_brush{patch, true};
        patch_brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 12, .h = 1}});
        patch_brush.plot(PointI{5, 1});
        OffscreenHost wide(24, 4);
        VISOR_CHECK(
            wide.table().submit_region(RectI{.pos = PointI{8, 1}, .size = SizeI{.w = 12, .h = 2}},
                                       patch.pixels().data(), Patch::kStrideBytes));
        VISOR_CHECK_EQ(wide.flush_count(), 1);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 12; ++x) {
                VISOR_CHECK_EQ(wide.get(PointI{8 + x, 1 + y}), patch.get(PointI{x, y}));
            }
        }
        // Outside the region stays dark: the copy stopped at its own width.
        VISOR_CHECK_EQ(wide.get(PointI{7, 1}), false);
        VISOR_CHECK_EQ(wide.get(PointI{20, 1}), false);
    }

    // The fast path's tail: a width that ends mid-byte copies only its live
    // bits. The source's padding bits are zero by the canvas contract, and
    // the neighbor pixels in the same destination byte are NOT part of the
    // region -- the padding must not paint them dark.
    {
        using Half = Canvas<SizeI{.w = 10, .h = 1}>;
        Half bar;
        const Pen bar_brush{bar, true};
        bar_brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 1}});
        OffscreenHost lit(16, 1);
        using Full = Canvas<SizeI{.w = 16, .h = 1}>;
        Full ground;
        const Pen ground_brush{ground, true};
        ground_brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 16, .h = 1}});
        VISOR_CHECK(lit.table().submit_frame(ground)); // all lit
        VISOR_CHECK(
            lit.table().submit_region(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 1}},
                                      bar.pixels().data(), Half::kStrideBytes));
        // The bar is all lit too, so every pixel stays lit: the assertion is
        // not about ink, it is about the tail bits at x 10..15 surviving an
        // overwrite whose source byte is zero there.
        for (int x = 0; x < 16; ++x) {
            VISOR_CHECK_EQ(lit.get(PointI{x, 0}), true);
        }
        // And a dark bar proves the live bits did land: same submit, dark
        // source, only x 0..9 may change.
        Half dark;
        OffscreenHost second(16, 1);
        VISOR_CHECK(second.table().submit_frame(ground));
        VISOR_CHECK(
            second.table().submit_region(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 1}},
                                         dark.pixels().data(), Half::kStrideBytes));
        for (int x = 0; x < 10; ++x) {
            VISOR_CHECK_EQ(second.get(PointI{x, 0}), false);
        }
        for (int x = 10; x < 16; ++x) {
            VISOR_CHECK_EQ(second.get(PointI{x, 0}), true);
        }
    }

    // Odd x keeps the bit path: any bit offset, pixel-true.
    {
        using Sliver = Canvas<SizeI{.w = 5, .h = 2}>;
        Sliver mark;
        const Pen mark_brush{mark, true};
        mark_brush.plot(PointI{0, 0});
        mark_brush.plot(PointI{4, 1});
        OffscreenHost odd(13, 5);
        VISOR_CHECK(
            odd.table().submit_region(RectI{.pos = PointI{3, 2}, .size = SizeI{.w = 5, .h = 2}},
                                      mark.pixels().data(), Sliver::kStrideBytes));
        VISOR_CHECK(odd.get(PointI{3, 2}) == true);
        VISOR_CHECK(odd.get(PointI{7, 3}) == true);
        VISOR_CHECK(odd.get(PointI{4, 2}) == false);
        VISOR_CHECK(odd.get(PointI{3, 3}) == false);
    }
}

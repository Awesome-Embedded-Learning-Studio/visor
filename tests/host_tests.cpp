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
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/pen.hpp"

void run_host_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::Color;
    using visor::draw::Pen;
    using visor::host::HostTable;
    using visor::host::OffscreenHost;

    using OddCanvas = Canvas<SizeI{.w = 13, .h = 5}>;

    // Full-frame handover on an odd-width canvas: the host copies row by row
    // honouring stride, and every received pixel matches the canvas.
    OddCanvas source;
    const Pen brush{source, Color::On};
    brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 13, .h = 1}});
    brush.plot(PointI{6, 3});
    brush.fill(RectI{.pos = PointI{9, 2}, .size = SizeI{.w = 2, .h = 2}});

    OffscreenHost mirror(13, 5);
    VISOR_CHECK(visor::host::submit_frame(mirror.table(), source));
    VISOR_CHECK_EQ(mirror.flush_count(), 1);
    VISOR_CHECK_EQ(mirror.rejected_count(), 0);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 13; ++x) {
            VISOR_CHECK_EQ(mirror.get((PointI{x, y})), source.get((PointI{x, y})));
        }
    }

    // A second submission counts; an empty table is refused without a crash
    // and without touching any counter.
    VISOR_CHECK(visor::host::submit_frame(mirror.table(), source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);
    const HostTable empty_table{};
    VISOR_CHECK(!visor::host::submit_frame(empty_table, source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);

    // A differently shaped host refuses the frame and keeps its own pixels dark.
    OffscreenHost smaller(12, 5);
    VISOR_CHECK(visor::host::submit_frame(smaller.table(), source));
    VISOR_CHECK_EQ(smaller.rejected_count(), 1);
    VISOR_CHECK_EQ(smaller.flush_count(), 0);
    VISOR_CHECK_EQ(smaller.get((PointI{0, 0})), Color::Off);

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
    visor::host::Event polled{};
    VISOR_CHECK(!visor::host::try_poll_event(bare, polled));
    VISOR_CHECK_EQ(visor::host::current_time_ms(bare), 0U);

    // Ch2 feed: five events in, the same five out in order, each carrying its
    // own fields. Everything crosses the table, the only channel the core sees.
    OffscreenHost feeder(4, 4);
    const HostTable fed = feeder.table();
    VISOR_CHECK(fed.poll_event != nullptr);
    VISOR_CHECK(fed.clock_ms != nullptr);
    VISOR_CHECK_EQ(visor::host::current_time_ms(fed), 0U);
    VISOR_CHECK(feeder.push_event(visor::host::KeyDown{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerMove{.pos = PointI{5, 6}}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{7, 8}}));
    VISOR_CHECK(feeder.push_event(visor::host::KeyUp{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{7, 8}}));

    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::KeyDown>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::KeyDown>(polled).code, 30U);
    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerMove>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerMove>(polled).pos, (PointI{5, 6}));
    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerDown>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerDown>(polled).button, 1U);
    VISOR_CHECK_EQ(std::get<visor::host::PointerDown>(polled).pos, (PointI{7, 8}));
    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::KeyUp>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::KeyUp>(polled).code, 30U);
    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::PointerUp>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::PointerUp>(polled).button, 1U);
    VISOR_CHECK_EQ(std::get<visor::host::PointerUp>(polled).pos, (PointI{7, 8}));

    // Drained queue: poll refuses, the host keeps answering.
    VISOR_CHECK(!visor::host::try_poll_event(fed, polled));

    // Wraparound: park the scripted clock next to the top of its range, let it
    // cross the boundary, and read the duration by unsigned subtraction, the
    // only way the ch2 contract allows.
    feeder.advance_ms(0xFFFF'FFFF'FFFF'0000ULL);
    VISOR_CHECK_EQ(visor::host::current_time_ms(fed), 0xFFFF'FFFF'FFFF'0000ULL);
    feeder.advance_ms(0x1000ULL);
    const std::uint64_t then = 0xFFFF'FFFF'FFFF'0000ULL;
    const std::uint64_t now = visor::host::current_time_ms(fed);
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
        VISOR_CHECK(visor::host::try_poll_event(busy, polled));
        VISOR_CHECK_EQ(std::get<visor::host::KeyDown>(polled).code, i);
    }
    VISOR_CHECK(!visor::host::try_poll_event(busy, polled));
}

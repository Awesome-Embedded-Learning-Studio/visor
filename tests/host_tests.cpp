#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/host.hpp"
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
}

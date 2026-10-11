#include <cstddef>
#include <cstdint>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"

void run_canvas_tests() {
    using visor::base::PointI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::Color;

    // 8 fits a byte per row, 13 needs padding: the two ends of the packing story.
    using ByteAlignedCanvas = Canvas<SizeI{.w = 8, .h = 2}>;
    using OddWidthCanvas = Canvas<SizeI{.w = 13, .h = 5}>;

    static_assert(ByteAlignedCanvas::kStrideBytes == 1UZ);
    static_assert(OddWidthCanvas::kStrideBytes == 2UZ);

    // A byte-aligned canvas fills whole bytes: set_clear On is all 0xFF, Off is all zeros.
    ByteAlignedCanvas aligned;
    aligned.set_clear(Color::On);
    VISOR_CHECK_EQ(aligned.pixels()[0], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(aligned.pixels()[1], std::uint8_t{0xFF});
    aligned.set_clear(Color::Off);
    VISOR_CHECK_EQ(aligned.pixels()[1], std::uint8_t{0});

    // Padding bits stay zero even after set_clear(On): on a 13-wide canvas the last byte
    // of every row is 0xF8, five pixel bits set and three padding bits clear.
    OddWidthCanvas odd;
    odd.set_clear(Color::On);
    for (std::size_t row = 0; row < OddWidthCanvas::kHeight; ++row) {
        VISOR_CHECK_EQ(odd.pixels()[row * OddWidthCanvas::kStrideBytes], std::uint8_t{0xFF});
        VISOR_CHECK_EQ(odd.pixels()[(row * OddWidthCanvas::kStrideBytes) + 1], std::uint8_t{0xF8});
    }
    VISOR_CHECK_EQ(odd.pixels().size(), OddWidthCanvas::kStrideBytes * OddWidthCanvas::kHeight);

    // Setting the origin touches exactly the top bit of the first byte.
    OddWidthCanvas single;
    VISOR_CHECK(single.set((PointI{0, 0}), Color::On));
    VISOR_CHECK_EQ(single.pixels()[0], std::uint8_t{0x80});

    // Round trip through set/get at the far corner; untouched pixels still read Off.
    VISOR_CHECK(single.set((PointI{12, 4}), Color::On));
    VISOR_CHECK_EQ(single.get((PointI{12, 4})), Color::On);
    VISOR_CHECK_EQ(single.get((PointI{0, 0})), Color::On);
    VISOR_CHECK_EQ(single.get((PointI{6, 2})), Color::Off);

    // Writing Off clears the bit again, the byte goes back to zero.
    VISOR_CHECK(single.set((PointI{0, 0}), Color::Off));
    VISOR_CHECK_EQ(single.pixels()[0], std::uint8_t{0});
    VISOR_CHECK_EQ(single.get((PointI{0, 0})), Color::Off);

    // Out-of-bounds coordinates: set refuses and touches nothing, get reads Off.
    OddWidthCanvas guard;
    VISOR_CHECK(guard.set((PointI{5, 2}), Color::On));
    const OddWidthCanvas snapshot = guard;
    VISOR_CHECK(!guard.set((PointI{-1, 0}), Color::On));
    VISOR_CHECK(!guard.set((PointI{0, -1}), Color::On));
    VISOR_CHECK(!guard.set((PointI{13, 0}), Color::On));
    VISOR_CHECK(!guard.set((PointI{0, 5}), Color::On));
    for (std::size_t i = 0; i < snapshot.pixels().size(); ++i) {
        VISOR_CHECK_EQ(guard.pixels()[i], snapshot.pixels()[i]);
    }
    VISOR_CHECK_EQ(guard.get((PointI{-1, 0})), Color::Off);
    VISOR_CHECK_EQ(guard.get((PointI{13, 0})), Color::Off);
    VISOR_CHECK_EQ(guard.get((PointI{0, 5})), Color::Off);
}

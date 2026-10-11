#include <array>
#include <cstdint>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/bit_ops/bit_ops.hpp"
#include "visor/base/bit_ops/bitmap.hpp"
#include "visor/base/bit_ops/bitmask.hpp"

void run_bit_tests() {
    using visor::base::bit::Bitmap;
    using visor::base::bit::BitMask;
    using visor::base::bit::BitRange;
    using visor::base::bit::WordMask;
    using Mask8 = BitMask<std::uint8_t>;

    // extract reads a field right-justified, deposit rewrites only that span.
    const BitRange field{.low = 4, .width = 3};
    Mask8 reg{0xB6}; // 1011_0110: field bits 6..4 hold 011, low nibble is 6
    VISOR_CHECK_EQ(reg.extract(field), std::uint8_t{3});
    reg.deposit(field, 5);
    VISOR_CHECK_EQ(reg.raw, std::uint8_t{0xD6});
    VISOR_CHECK_EQ(reg.extract(field), std::uint8_t{5});
    VISOR_CHECK_EQ(reg.extract((BitRange{.low = 0, .width = 4})), std::uint8_t{6}); // untouched

    // Single-bit masks, union, saturating ones.
    VISOR_CHECK(reg.has(visor::base::bit::mask_bit<std::uint8_t>(1)));
    VISOR_CHECK(!reg.has(visor::base::bit::mask_bit<std::uint8_t>(0)));
    VISOR_CHECK_EQ((Mask8{0x0F} | Mask8{0x30}).raw, std::uint8_t{0x3F});
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint8_t>(3), std::uint8_t{0x07});
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint8_t>(8), std::uint8_t{0xFF});
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint64_t>(64), ~std::uint64_t{0});

    // Slice helpers.
    VISOR_CHECK_EQ(visor::base::bit::low_nibble(0xB6), 6);
    VISOR_CHECK_EQ(visor::base::bit::high_nibble(0xB6), 0xB);
    VISOR_CHECK_EQ(visor::base::bit::low_byte(0x1234), 0x34);
    VISOR_CHECK_EQ(visor::base::bit::high_byte(0x1234), 0x12);
    VISOR_CHECK_EQ(visor::base::bit::nibble_at(0x5678, 8), 6);

    // Bitmap over caller storage: capacity 100 spans two 64-bit words, so the
    // tail word is masked and runs may cross the word boundary.
    std::array<WordMask, Bitmap::words_for(100)> storage{};
    Bitmap map;
    map.init(storage.data(), 100);
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{0});

    map.set(0);
    map.set(63);
    map.set(64);
    map.set(99);
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{4});
    VISOR_CHECK(map.test(0));
    VISOR_CHECK(map.test(63));
    VISOR_CHECK(map.test(99));
    VISOR_CHECK(!map.test(1));
    map.set(63); // already set, the count stays honest
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{4});
    map.clear(63);
    VISOR_CHECK(!map.test(63));
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{3});

    map.set_range(10, 5);
    VISOR_CHECK(map.test(14));
    VISOR_CHECK(!map.test(15));
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{8});
    map.clear_range(10, 5);
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{3});

    // First-fit runs: bits 0 and 2 set makes bit 1 the first single, 3 the first pair.
    map.clear_all();
    map.set(0);
    map.set(2);
    VISOR_CHECK_EQ(map.find_clear_run(1), std::uint64_t{1});
    VISOR_CHECK_EQ(map.find_clear_run(2), std::uint64_t{3});

    // A run may straddle the word boundary: bits 0..62 set, 63 and 64 clear.
    map.clear_all();
    map.set_range(0, 63);
    VISOR_CHECK_EQ(map.find_clear_run(2), std::uint64_t{63});

    // Whole-capacity fill keeps the tail word honest at the edge.
    map.set_all();
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{100});
    VISOR_CHECK_EQ(map.clear_count(), std::uint64_t{0});
    VISOR_CHECK_EQ(map.find_clear_run(1), visor::base::bit::kInvalidIndex);
    map.clear_all();
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{0});
    VISOR_CHECK_EQ(map.find_clear_run(101), visor::base::bit::kInvalidIndex);
}

/**
 * @file    bitmap.hpp
 * @brief   A caller-backed bitmap over any number of bits.
 *
 * One flat word array does the bookkeeping; the storage belongs to the
 * owner, so base stays allocation-free. Single bits ride the BitMask
 * vocabulary: positions turn into masks in exactly one place.
 *
 * Ported from Cinux base/include/cinux/bit_ops (2026-10-07): the word width
 * is std::uint64_t by name, because Cinux's unsigned long is 64-bit on the
 * development PC but 32-bit on ARM hosts and the /64 word math here assumes
 * 64-bit words on every host.
 *
 * @author  Charliechen114514
 * @date    2026-10-07
 * @version 0.1
 * @since   0.1.0
 * @ingroup base_bit_ops
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "visor/base/bit_ops/bitmask.hpp"

namespace visor::base::bit {

/** @brief Sentinel returned when no run of the wanted length exists. */
inline constexpr std::uint64_t kInvalidIndex = ~std::uint64_t{0};

/** @brief Storage word type of every bitmap. */
using WordMask = BitMask<std::uint64_t>;

/**
 * @brief   A set of bits over caller-provided words.
 * @note    Capacity counts bits, not words; the tail word is masked so
 *          whole-capacity operations stay honest at the edge.
 * @since   0.1.0
 * @ingroup base_bit_ops
 */
class Bitmap {
  public:
    /**
     * @brief   Words needed to hold a capacity of bits.
     *
     * @param[in] bit_capacity   Bit count to cover.
     * @return  (bit_capacity + 63) / 64; zero capacity needs zero words.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    static constexpr std::uint64_t words_for(std::uint64_t bit_capacity) {
        return (bit_capacity + 63) / 64;
    }

    /**
     * @brief   Binds storage and starts every bit clear.
     *
     * @param[in] words         Caller-owned word array, at least
     *                          words_for(bit_capacity) entries.
     * @param[in] bit_capacity  Number of bits the bitmap governs.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    void init(WordMask* words, std::uint64_t bit_capacity) {
        words_ = words;
        capacity_ = bit_capacity;
        count_set_ = 0;
        clear_all();
    }

    /** @brief Marks one bit; already-set bits keep the count honest. */
    void set(std::uint64_t index) {
        WordMask& word = words_[index / 64];
        if (!word.has(mask_bit<std::uint64_t>(static_cast<std::uint8_t>(index % 64)))) {
            word.deposit(bit_at(static_cast<std::uint8_t>(index % 64)), 1);
            ++count_set_;
        }
    }

    /** @brief Clears one bit; already-clear bits keep the count honest. */
    void clear(std::uint64_t index) {
        WordMask& word = words_[index / 64];
        if (word.has(mask_bit<std::uint64_t>(static_cast<std::uint8_t>(index % 64)))) {
            word.deposit(bit_at(static_cast<std::uint8_t>(index % 64)), 0);
            --count_set_;
        }
    }

    /** @brief Reports whether one bit is set. */
    [[nodiscard]] bool test(std::uint64_t index) const {
        return words_[index / 64].has(
            mask_bit<std::uint64_t>(static_cast<std::uint8_t>(index % 64)));
    }

    /**
     * @brief   Marks count consecutive bits from base.
     *
     * @param[in] base   First bit index.
     * @param[in] count  Bits to mark.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    void set_range(std::uint64_t base, std::uint64_t count) {
        for (std::uint64_t index = base; index < base + count; ++index) {
            set(index);
        }
    }

    /**
     * @brief   Clears count consecutive bits from base.
     *
     * @param[in] base   First bit index.
     * @param[in] count  Bits to clear.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    void clear_range(std::uint64_t base, std::uint64_t count) {
        for (std::uint64_t index = base; index < base + count; ++index) {
            clear(index);
        }
    }

    /**
     * @brief   Sets every bit up to capacity.
     *
     * @note    The tail word is masked to capacity, so bits past the
     *          end never pretend to exist.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    void set_all() {
        if (capacity_ == 0) {
            return;
        }
        const std::uint64_t words = words_for(capacity_);
        const auto tail_bits = static_cast<std::uint8_t>(capacity_ % 64);
        for (std::uint64_t word_index = 0; word_index < words; ++word_index) {
            words_[word_index] = WordMask{ones<std::uint64_t>(64)};
        }
        if (tail_bits != 0) {
            words_[words - 1] = WordMask{ones<std::uint64_t>(tail_bits)};
        }
        count_set_ = capacity_;
    }

    /** @brief Clears every bit up to capacity. */
    void clear_all() {
        if (capacity_ == 0) {
            return;
        }
        const std::uint64_t words = words_for(capacity_);
        for (std::uint64_t word_index = 0; word_index < words; ++word_index) {
            words_[word_index] = WordMask{0};
        }
        count_set_ = 0;
    }

    /** @brief How many bits are set. */
    [[nodiscard]] std::uint64_t set_count() const { return count_set_; }

    /** @brief How many bits are clear. */
    [[nodiscard]] std::uint64_t clear_count() const { return capacity_ - count_set_; }

    /**
     * @brief   Finds the first run of consecutive clear bits.
     *
     * @param[in] length   Run length wanted; zero or over-capacity asks
     *                     fail immediately.
     * @return  Index of the run start, or kInvalidIndex when none fits.
     * @note    Words with no clear bit are skipped whole.
     * @since   0.1.0
     * @ingroup base_bit_ops
     */
    [[nodiscard]] std::uint64_t find_clear_run(std::uint64_t length) const {
        if (length == 0 || length > capacity_) {
            return kInvalidIndex;
        }
        std::uint64_t run = 0;
        const std::uint64_t words = words_for(capacity_);
        for (std::uint64_t word_index = 0; word_index < words; ++word_index) {
            if (words_[word_index].raw == ones<std::uint64_t>(64)) {
                run = 0;
                continue;
            }
            for (std::uint8_t bit = 0; bit < 64; ++bit) {
                const std::uint64_t bit_index = (word_index * 64) + bit;
                if (bit_index >= capacity_) {
                    break;
                }
                if (test(bit_index)) {
                    run = 0;
                    continue;
                }
                if (++run == length) {
                    return (bit_index + 1) - length;
                }
            }
        }
        return kInvalidIndex;
    }

  private:
    WordMask* words_ = nullptr;
    std::uint64_t capacity_ = 0;
    std::uint64_t count_set_ = 0;
};

} // namespace visor::base::bit

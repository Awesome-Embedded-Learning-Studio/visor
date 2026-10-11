/**
 * @file    ring_queue.hpp
 * @brief   A bounded FIFO ring: one slot stays empty so full and empty stay
 *          distinguishable, and a full queue refuses the newcomer.
 *
 * Ported from Cinux base/include/cinux/container/ring_queue.hpp (2026-10-11):
 * namespace, naming and noexcept follow visor rules. The concurrency stance
 * carries over unchanged: a plain structure with no opinion about threads.
 * The ch2 event loop is single-threaded, so the queue needs no lock here; the
 * cross-thread upgrade (atomic head and tail) lands with the first threaded
 * host, and the TSan acceptance lane is already in place for that day.
 *
 * @author  Charliechen114514
 * @date    2026-10-11
 * @version 0.1
 * @since   0.1.0
 * @ingroup base_containers
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

namespace visor::base::container {

/**
 * @brief   A bounded FIFO ring over kCapacity-1 usable slots.
 *
 * Overflow policy: push returns false on a full queue and the newcomer is
 * dropped, the residents untouched. Dropping an event is the host's decision
 * to make, never the queue's.
 *
 * @tparam  Element    What travels through the queue.
 * @tparam  kCapacity  Total slot count; one stays empty so full and empty
 *                     never share a layout.
 * @since   0.1.0
 * @ingroup base_containers
 */
template <typename Element, std::size_t kCapacity>
class RingQueue {
    // These are promises about what may travel, so they are checked where the
    // promise is made: a non-copyable element fails here, not inside a test.
    static_assert(std::is_trivially_copyable_v<Element>,
                  "RingQueue stores plain values only: it is a fixed block of slots, not a heap");
    static_assert(kCapacity >= 2,
                  "One slot is sacrificed to tell full from empty, so two is the useful minimum");

  public:
    /**
     * @brief     Offers one element to the queue.
     * @param[in] value Element to store.
     * @return    True when stored; false when the queue was full and the
     *            element was dropped.
     */
    [[nodiscard]] bool push(Element value) noexcept {
        const std::size_t next = advance(head_);
        if (next == tail_) {
            return false;
        }
        slots_[head_] = value;
        head_ = next;
        return true;
    }

    /**
     * @brief      Takes the oldest element.
     * @param[out] out Where the element lands, untouched when empty.
     * @return     True when an element was there; false when empty.
     */
    [[nodiscard]] bool pop(Element& out) noexcept {
        if (tail_ == head_) {
            return false;
        }
        out = slots_[tail_];
        tail_ = advance(tail_);
        return true;
    }

    /// @brief How many elements wait.
    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return (head_ + kCapacity - tail_) % kCapacity;
    }

  private:
    /// @brief Steps one slot forward, wrapping at the end of the array.
    static constexpr std::size_t advance(std::size_t slot) noexcept {
        return (slot + 1U) % kCapacity;
    }

    std::array<Element, kCapacity> slots_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

} // namespace visor::base::container

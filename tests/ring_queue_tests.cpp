#include <cstdint>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/containers/ring_queue.hpp"

void run_ring_queue_tests() {
    using visor::base::container::RingQueue;

    int value = 0;

    // FIFO order: first in, first out, and count tracks the residents.
    RingQueue<int, 4> fifo;
    VISOR_CHECK(fifo.push(1));
    VISOR_CHECK(fifo.push(2));
    VISOR_CHECK(fifo.push(3));
    VISOR_CHECK_EQ(fifo.count(), 3U);
    VISOR_CHECK(fifo.pop(value));
    VISOR_CHECK_EQ(value, 1);
    VISOR_CHECK(fifo.pop(value));
    VISOR_CHECK_EQ(value, 2);
    VISOR_CHECK(fifo.pop(value));
    VISOR_CHECK_EQ(value, 3);
    VISOR_CHECK_EQ(fifo.count(), 0U);

    // Empty queue: pop refuses, nothing crashes.
    VISOR_CHECK(!fifo.pop(value));

    // Full policy: one slot stays empty so full and empty stay distinguishable,
    // meaning a 4-slot queue holds 3. The newcomer is refused, the residents
    // keep their order and their values.
    RingQueue<int, 4> bounded;
    VISOR_CHECK(bounded.push(10));
    VISOR_CHECK(bounded.push(11));
    VISOR_CHECK(bounded.push(12));
    VISOR_CHECK(!bounded.push(99));
    VISOR_CHECK_EQ(bounded.count(), 3U);
    VISOR_CHECK(bounded.pop(value));
    VISOR_CHECK_EQ(value, 10);
    VISOR_CHECK(bounded.pop(value));
    VISOR_CHECK_EQ(value, 11);
    VISOR_CHECK(bounded.pop(value));
    VISOR_CHECK_EQ(value, 12);
    VISOR_CHECK(!bounded.pop(value));

    // The sacrifice is visible at the minimum: a 2-slot queue holds exactly one.
    RingQueue<int, 2> single;
    VISOR_CHECK(single.push(7));
    VISOR_CHECK(!single.push(8));
    VISOR_CHECK(single.pop(value));
    VISOR_CHECK_EQ(value, 7);

    // Wraparound: refill after every drain so head and tail cross the end of
    // the storage array lap after lap; order survives each one.
    RingQueue<int, 3> carousel;
    for (int lap = 0; lap < 5; ++lap) {
        const int first = lap * 10;
        const int second = first + 1;
        VISOR_CHECK(carousel.push(first));
        VISOR_CHECK(carousel.push(second));
        VISOR_CHECK(!carousel.push(99)); // 2 usable slots in a 3-slot queue
        VISOR_CHECK_EQ(carousel.count(), 2U);
        int out_first = 0;
        int out_second = 0;
        VISOR_CHECK(carousel.pop(out_first));
        VISOR_CHECK(carousel.pop(out_second));
        VISOR_CHECK(!carousel.pop(out_first));
        VISOR_CHECK_EQ(out_first, first);
        VISOR_CHECK_EQ(out_second, second);
    }

    // Struct payload: fields survive the by-value trip in and the copy out.
    struct Tick {
        std::uint32_t seq;
        int code;
    };
    RingQueue<Tick, 3> ticks;
    VISOR_CHECK((ticks.push(Tick{.seq = 1, .code = 42})));
    VISOR_CHECK((ticks.push(Tick{.seq = 2, .code = 43})));
    Tick out{};
    VISOR_CHECK(ticks.pop(out));
    VISOR_CHECK_EQ(out.seq, 1U);
    VISOR_CHECK_EQ(out.code, 42);
    VISOR_CHECK(ticks.pop(out));
    VISOR_CHECK_EQ(out.seq, 2U);
    VISOR_CHECK_EQ(out.code, 43);
}

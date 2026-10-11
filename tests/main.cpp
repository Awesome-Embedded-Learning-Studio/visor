#include "cases.hpp"
#include "testing.hpp"

int main() {
    run_bit_tests();
    run_canvas_tests();
    run_diff_tests();
    run_host_tests();
    run_pen_tests();
    run_point_tests();
    run_rect_tests();
    run_ring_queue_tests();
    run_smoke_tests();
    run_widget_tests();
    return ::visor::testing::failure_count() == 0 ? 0 : 1;
}

#include "cases.hpp"
#include "testing.hpp"

int main() {
    run_bitmap_tests();
    run_bit_tests();
    run_canvas_tests();
    run_cinux_tests();
    run_choice_tests();
    run_control_tests();
    run_diff_tests();
    run_double_click_tests();
    run_evdev_tests();
    run_event_tests();
    run_font_tests();
    run_gesture_tests();
    run_host_tests();
    run_id_session_tests();
    run_layout_tests();
    run_pen_tests();
    run_point_tests();
    run_rect_tests();
    run_render_tests();
    run_ring_queue_tests();
    run_run_loop_tests();
    run_smoke_tests();
    run_text_tests();
    run_theme_tests();
    run_ssd1306_tests();
    run_utf8_tests();
    run_widget_tests();
    run_x11_host_tests();
    run_x11_tests();
    return ::visor::testing::failure_count() == 0 ? 0 : 1;
}

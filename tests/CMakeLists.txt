# Single verification entry: every case links into visor.tests, CTest sees one executable.
# Tests include the library the way a real consumer does, "visor/..." via visor.core's
# interface include path, with no private search path of their own.
add_executable(visor.tests main.cpp bitmap_tests.cpp bit_tests.cpp canvas_tests.cpp cinux_tests.cpp choice_tests.cpp control_tests.cpp cross_tests.cpp diff_tests.cpp double_click_tests.cpp evdev_tests.cpp event_tests.cpp font_tests.cpp gesture_tests.cpp host_tests.cpp id_session_tests.cpp layout_tests.cpp pen_tests.cpp pixel_tests.cpp point_tests.cpp popup_tests.cpp rect_tests.cpp render_tests.cpp ring_queue_tests.cpp run_loop_tests.cpp smoke_tests.cpp ssd1306_tests.cpp submit_plan_tests.cpp text_tests.cpp theme_tests.cpp utf8_tests.cpp widget_tests.cpp wayland_host_tests.cpp wayland_tests.cpp x11_host_tests.cpp x11_tests.cpp)
target_link_libraries(visor.tests PRIVATE visor.host.offscreen visor.host.cinux visor.host.evdev visor.host.wayland visor.host.x11 visor.ui)

add_test(NAME visor.tests COMMAND visor.tests)

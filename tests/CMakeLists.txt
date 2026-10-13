# Single verification entry: every case links into visor.tests, CTest sees one executable.
# Tests include the library the way a real consumer does, "visor/..." via visor.core's
# interface include path, with no private search path of their own.
add_executable(visor.tests main.cpp bitmap_tests.cpp bit_tests.cpp canvas_tests.cpp control_tests.cpp diff_tests.cpp font_tests.cpp gesture_tests.cpp host_tests.cpp pen_tests.cpp point_tests.cpp rect_tests.cpp render_tests.cpp ring_queue_tests.cpp run_loop_tests.cpp smoke_tests.cpp ssd1306_tests.cpp utf8_tests.cpp widget_tests.cpp)
target_link_libraries(visor.tests PRIVATE visor.host.offscreen visor.ui)

add_test(NAME visor.tests COMMAND visor.tests)

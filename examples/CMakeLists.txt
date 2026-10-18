# Demo binaries exercising the hosts the way a reader would: build, run, look.
add_executable(x11_window x11_window.cpp)
target_link_libraries(x11_window PRIVATE visor.host.x11 visor.host visor.ui)

# The live signal lab: the same window host, an animated scene, and a
# headless self-test so the frame loop has a judgment of its own.
add_executable(signal signal.cpp)
target_link_libraries(signal PRIVATE visor.host.x11 visor.host.offscreen visor.host visor.ui)
add_test(NAME visor.signal.selftest COMMAND signal --self-test)

# The snake: a fixed-step simulation, a two-slot turn buffer, and a board
# repaint per step -- the frame loop's second judgment.
add_executable(snake snake.cpp)
target_link_libraries(snake PRIVATE visor.host.x11 visor.host.offscreen visor.host visor.ui)
add_test(NAME visor.snake.selftest COMMAND snake --self-test)

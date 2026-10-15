# Demo binaries exercising the hosts the way a reader would: build, run, look.
add_executable(x11_window x11_window.cpp)
target_link_libraries(x11_window PRIVATE visor.host.x11 visor.host visor.ui)

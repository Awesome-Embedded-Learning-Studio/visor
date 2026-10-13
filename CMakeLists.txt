cmake_minimum_required(VERSION 3.28)
project(visor LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 modules are unused, disable module dependency scanning so compile_commands.json
# carries no GCC-only flags and clang-tidy/clangd parse it cleanly.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(VISOR_SANITIZE "ASan+UBSan 开关" OFF)
option(VISOR_TSAN "TSan 开关" OFF)

# The two sanitizers are configure-time exclusive: their runtime libraries
# cannot load into one process, so the conflict fails here, not at link time.
if(VISOR_SANITIZE AND VISOR_TSAN)
    message(FATAL_ERROR "VISOR_SANITIZE 与 VISOR_TSAN 互斥：分目录构建，各自只开一个")
endif()

add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)

if(VISOR_SANITIZE)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

if(VISOR_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# Optional developer tooling. Each check target is created only when its tool is
# present, so a plain configure, build and test run never requires them. No version
# pinning: a tool's version decides its own output, drift across versions is accepted.
find_program(VISOR_CLANG_FORMAT NAMES clang-format)
find_program(VISOR_CLANG_TIDY NAMES clang-tidy)
find_program(VISOR_DOXYGEN NAMES doxygen)

# GUI core: platform-independent header-only library. Everything public lives under
# include/visor and consumers include "visor/...". src/bindings carries one directory
# per host (B4 principle): the contract pieces sit at the top, each host
# implementation below its own leaf. The ui layer stays flat in src/ui.
add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${CMAKE_SOURCE_DIR}/include)
add_library(visor.host STATIC src/bindings/host.cpp)
target_link_libraries(visor.host PUBLIC visor.core)
add_library(visor.host.offscreen STATIC src/bindings/offscreen/offscreen_host.cpp)
target_link_libraries(visor.host.offscreen PUBLIC visor.host)
# The Cinux binding's pure half: frame expansion and event translation, free
# of mmap and ioctl so PC judgments drive them. The adapter that reads
# /dev/fb0 and /dev/event0 lives in the CinuxOS tree and links this in.
add_library(visor.host.cinux STATIC src/bindings/cinux/frame_expand.cpp
                                    src/bindings/cinux/event_wire.cpp)
target_link_libraries(visor.host.cinux PUBLIC visor.core)
add_library(
    visor.ui STATIC
    src/ui/widgets/widget.cpp
    src/ui/primitives/rectangle/rectangle.cpp
    src/ui/widgets/button/button.cpp
    src/ui/widgets/checkbox/checkbox.cpp
    src/ui/widgets/label/label.cpp
    src/ui/widgets/box/box.cpp
    src/ui/widgets/slider/slider.cpp
    src/ui/widgets/textbox/textbox.cpp
    src/ui/widgets/radio/radio.cpp
    src/ui/widgets/dropdown/dropdown.cpp
    src/ui/dirty_regions.cpp
    src/ui/input_router.cpp
    src/ui/text.cpp)
target_link_libraries(visor.ui PUBLIC visor.core)

file(GLOB_RECURSE VISOR_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/include/*.hpp
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp)

if(VISOR_CLANG_FORMAT)
    add_custom_target(visor.check-format
        COMMAND ${VISOR_CLANG_FORMAT} --dry-run --Werror ${VISOR_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format 检查格式")
endif()

file(GLOB_RECURSE VISOR_TIDY_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.cpp)

if(VISOR_CLANG_TIDY)
    add_custom_target(visor.check-tidy
        COMMAND ${VISOR_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR} ${VISOR_TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy 静态检查")
endif()

if(VISOR_DOXYGEN)
    add_custom_target(visor.docs
        COMMAND ${VISOR_DOXYGEN} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Doxygen 生成 API 文档")
endif()

enable_testing()
add_subdirectory(tests)

# Standalone build for the font/bitmap conversion tool. Deliberately independent of the
# top-level project: no reference from it, no dependency on the GUI core. Proving this
# independence is part of the batch's acceptance.
cmake_minimum_required(VERSION 3.28)
project(visor.fontgen LANGUAGES CXX VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)

# The conversion logic as a library, so the CLI and the tests link the same
# code the way real consumers would.
add_library(fontgen.core STATIC src/emit.cpp src/image.cpp src/pbm.cpp)
target_include_directories(fontgen.core PUBLIC src)

add_executable(fontgen src/main.cpp)
target_link_libraries(fontgen PRIVATE fontgen.core)

enable_testing()
add_executable(fontgen.tests tests/main.cpp tests/emit_tests.cpp tests/pbm_tests.cpp
                            tests/cli_tests.cpp)
target_link_libraries(fontgen.tests PRIVATE fontgen.core)
add_test(NAME fontgen.units COMMAND fontgen.tests)
# The CLI cases run the real binary as a subprocess; the generator expression
# hands its path over, and WORKING_DIRECTORY keeps the scratch files inside
# the build tree.
add_test(NAME fontgen.cli COMMAND fontgen.tests --cli $<TARGET_FILE:fontgen>
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

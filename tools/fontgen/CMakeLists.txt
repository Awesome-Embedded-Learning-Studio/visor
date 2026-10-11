# Standalone build for the font/bitmap conversion tool. Deliberately independent of the
# top-level project: no reference from it, no dependency on the GUI core. Proving this
# independence is part of the batch's acceptance.
cmake_minimum_required(VERSION 3.28)
project(visor.fontgen LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)

enable_testing()
add_executable(fontgen src/main.cpp)
add_test(NAME fontgen.smoke COMMAND fontgen)

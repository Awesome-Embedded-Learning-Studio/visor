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
add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)
if(VISOR_SANITIZE)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Tool version locks: format and tidy share LLVM 22.1.8, docs use Doxygen 1.18.0, see document/toolchain.md.
find_program(VISOR_CLANG_FORMAT NAMES clang-format)
if(NOT VISOR_CLANG_FORMAT)
    message(FATAL_ERROR "未找到 clang-format，需要 LLVM 22.1.8 工具集")
endif()
execute_process(
    COMMAND ${VISOR_CLANG_FORMAT} --version
    OUTPUT_VARIABLE visor_format_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT visor_format_version MATCHES "22\\.1\\.8")
    message(FATAL_ERROR "clang-format 必须是 22.1.8，读到 ${visor_format_version}")
endif()

find_program(VISOR_CLANG_TIDY NAMES clang-tidy)
if(NOT VISOR_CLANG_TIDY)
    message(FATAL_ERROR "未找到 clang-tidy，需要 LLVM 22.1.8 工具集")
endif()
execute_process(
    COMMAND ${VISOR_CLANG_TIDY} --version
    OUTPUT_VARIABLE visor_tidy_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT visor_tidy_version MATCHES "22\\.1\\.8")
    message(FATAL_ERROR "clang-tidy 必须是 22.1.8，读到 ${visor_tidy_version}")
endif()

find_program(VISOR_DOXYGEN NAMES doxygen)
if(NOT VISOR_DOXYGEN)
    message(FATAL_ERROR "未找到 doxygen，需要 1.18.0")
endif()
execute_process(
    COMMAND ${VISOR_DOXYGEN} --version
    OUTPUT_VARIABLE visor_doxygen_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT visor_doxygen_version MATCHES "^1\\.18\\.0$")
    message(FATAL_ERROR "Doxygen 必须是 1.18.0，读到 ${visor_doxygen_version}")
endif()

# GUI core: platform-independent header-only library. Everything public lives under
# include/visor and consumers include "visor/...". src/ carries implementation files of
# the host-side bindings, collected into visor.host.
add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${CMAKE_SOURCE_DIR}/include)
add_library(visor.host STATIC src/bindings/host.cpp)
target_link_libraries(visor.host PUBLIC visor.core)

file(GLOB_RECURSE VISOR_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/include/*.hpp
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_custom_target(visor.check-format
    COMMAND ${VISOR_CLANG_FORMAT} --dry-run --Werror ${VISOR_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format 22.1.8 检查格式")

file(GLOB_RECURSE VISOR_TIDY_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_custom_target(visor.check-tidy
    COMMAND ${VISOR_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR} ${VISOR_TIDY_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-tidy 22.1.8 静态检查")

add_custom_target(visor.docs
    COMMAND ${VISOR_DOXYGEN} ${CMAKE_SOURCE_DIR}/document/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Doxygen 生成 API 文档到 document/api")

enable_testing()
add_subdirectory(tests)

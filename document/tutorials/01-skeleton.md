# 第一章 起步与骨架：先把验证立起来

这一章不写一行 GUI 代码。在画第一个像素之前，我们要先把“每写一批，就能用一条命令验证一批”的骨架立起来：仓库怎么组织、CMake 怎么配置、测试从哪里进、格式和静态检查怎么跑、API 文档怎么生成。visor 是教学工程，价值在过程本身，而过程可教的前提是每一步都能跑、能看、能被工具逮住错误。作者在之前的工程里养成的习惯是完成一批立刻验证、立刻记录，这一章就是把那个习惯固化进仓库结构。

批次一（B1）的完整计划与当日实录见文末参考里的计划与批记。批记是每批完成当天写下的记录：验证跑了什么、接口怎么定的、哪些事还没定。本章按仓库现状带你走一遍。

## 仓库在第一天长什么样

我们把全部公共头文件放进 `include/visor/`，宿主侧的实现文件放进 `src/bindings/`，测试放进 `tests/`，取模工具独立住在 `tools/fontgen/`，教程与工具链名册放进 `document/`。

```
CMakeLists.txt        顶层构建与全部验证目标
.clang-format         格式规则
.clang-tidy           静态检查与命名规则
include/visor/        全部公共头文件（GUI 核心）
src/bindings/         宿主侧实现（第六章起住进内容）
tests/                统一测试入口
tools/fontgen/        取模工具的独立目录
document/             教程、工具链名册、API 文档配置（Doxyfile）与生成产物
```

头文件统一住在 `include/` 下，消费方一律写 `#include "visor/..."`。这条规则从测试就开始守：测试像真实使用者一样包含库的头，不给自己开私有的搜索路径。这样 public 与 private 的分界从第一天就是真实的，不需要以后再补承诺。

## 顶层 CMakeLists 逐段过

顶层 `CMakeLists.txt` 是整个骨架的中枢，我们按现状逐段读。

### 语言与编译旗子

```cmake
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
```

几个决定值得停一下。

语言定在 C++23，三个旗子禁异常、禁 RTTI、禁静态变量的线程安全防护。visor 的最终落脚点里有裸机 MCU（微控制器，板上资源以 KB 计），异常与运行时类型信息在那边是奢侈品。PC 侧同样加上这三面旗，让 PC 上写的每一行代码从第一天起就活在裸机也接受的子集里，等移植那天不需要回来改写法。

`-Wall -Wextra -Werror` 把警告当错误。教学工程的警告一旦放过，就会在下一批被人当成“本来就有的”。

`CMAKE_EXPORT_COMPILE_COMMANDS` 导出 `compile_commands.json`（每个翻译单元一条完整编译命令的数据库），clang-tidy 和编辑器里的 clangd 语言服务都依赖它。跟着的 `CMAKE_CXX_SCAN_FOR_MODULES OFF` 背后有一段教训，注释原文写着：关掉 C++20 模块依赖扫描，compile_commands 里就不会混进 GCC 独有的旗子，clang-tidy 和 clangd 才能干净地解析它。

`VISOR_SANITIZE` 是 ASan（AddressSanitizer，内存错误检测器）与 UBSan（UndefinedBehaviorSanitizer，未定义行为检测器）的总开关。它默认关闭，验收关卡那天我们会用它配置一个独立的构建目录，普通构建目录保持干净。

### 工具版本锁

接下来一段是三个工具的版本检查，以 clang-format 为例：

```cmake
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
```

clang-tidy 22.1.8 与 Doxygen 1.18.0 各有一段一模一样结构的检查。configure 阶段版本不合就 `FATAL_ERROR`，构建根本不会开始。

为什么把版本卡得这么死？格式检查的意义在于“同一份规则对任何人产出同一份结果”，而 clang-format 的小版本之间就可能改判。让 22.1.8 之外的版本跑检查，检查结果会漂移，绿灯就不再说明任何事。所以版本在 configure 时硬卡，全部工具的版本与角色记在 `document/toolchain.md` 名册里，那一份是权威。

### 两个库目标

```cmake
# GUI core: platform-independent header-only library. Everything public lives under
# include/visor and consumers include "visor/...". src/ carries implementation files of
# the host-side bindings, collected into visor.host.
add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${CMAKE_SOURCE_DIR}/include)
add_library(visor.host STATIC src/bindings/host.cpp)
target_link_libraries(visor.host PUBLIC visor.core)
```

`visor.core` 是 INTERFACE 库（不产出任何编译产物，只向外转发包含路径），GUI 核心在第一章是纯头文件。`visor.host` 是静态库，把宿主侧的实现文件收拢起来，第六章它会住进第一份实现。跟跑的你此刻还没有宿主代码，先 `touch src/bindings/host.cpp` 放一个空文件占位，让构建链完整。

核心与宿主分成两个目标，是“核心禁平台头”这条边界的具体体现。以后谁想在核心里 include 一个平台专属头，得先跨过“这段代码到底属于哪个目标”的疑问，边界越早有形越好守。

## 测试要先有断言

visor 全程零第三方依赖，测试断言是手写的。整个框架住在一个头文件 `tests/testing.hpp` 里：

```cpp
#pragma once

#include <cstdio>
#include <print>

namespace visor::testing {

/// Cumulative count of failed checks, zero when everything passed. main() maps it to the exit code.
inline int& failure_count() {
    static int count = 0;
    return count;
}

/// Prints one failure line and bumps the counter. Called by the assert macros, not by test bodies.
inline void report(const char* file, int line, const char* detail) {
    ++failure_count();
    std::println(stderr, "FAIL {}:{} {}", file, line, detail);
}

} // namespace visor::testing

/// On a false condition, prints the expression text and counts. No exceptions, no abort.
#define VISOR_CHECK(cond)                                        \
    do {                                                         \
        if (!(cond)) {                                           \
            ::visor::testing::report(__FILE__, __LINE__, #cond); \
        }                                                        \
    } while (false)

/// Asserts both values are equal, prints both expression texts and counts on mismatch.
#define VISOR_CHECK_EQ(actual, expected)                                            \
    do {                                                                            \
        const auto& visor_check_actual_ = (actual);                                 \
        const auto& visor_check_expected_ = (expected);                             \
        if (!(visor_check_actual_ == visor_check_expected_)) {                      \
            ::visor::testing::report(__FILE__, __LINE__, #actual " == " #expected); \
        }                                                        \
    } while (false)
```

断言的形态是被“禁异常”逼出来的。常见的测试框架靠抛异常终止用例，我们这里不能抛，于是失败只打印一行 `FAIL 文件:行 表达式` 并累加计数，`main` 最后看计数决定退出码。一个用例失败不会打断后面的用例，整份测试跑完你能看到全部失败点。

这里有一次链接器直接报错的经历。最初这几个函数打算声明放头文件、实现放一个 `tests/testing.cpp`，有一版把计数函数的实现忘了写上。编译一路绿灯，每个翻译单元各看各的，声明齐了就没人报错，直到最后拼装可执行文件那一步，链接器打出 `undefined reference to visor::testing::failure_count()`。声明的存在编译器看得见，实现的缺失只有链接器看得见。后来我们把两个函数都改成 `inline` 直接住进头文件，这一类问题从根上消失，也就有了你上面读到的形态。

### 用例注册与入口

每个测试文件暴露一个 `run_*_tests()` 函数，登记在 `tests/cases.hpp`。这一节贴的三份文件（`cases.hpp`、`main.cpp`、`tests/CMakeLists.txt`）都是七个测试文件全部登记之后的终态。第一章此刻 `cases.hpp` 里只有 `run_smoke_tests()` 一行声明，`main.cpp` 只调用它一次，`add_executable` 只列 `main.cpp` 与 `smoke_tests.cpp` 两个文件，后面每章各自登记：

```cpp
#pragma once

// Registry of test entry points. Add one declaration per new test file, main.cpp calls each in
// turn.
void run_bit_tests();
void run_canvas_tests();
void run_diff_tests();
void run_host_tests();
void run_pen_tests();
void run_point_tests();
void run_rect_tests();
void run_smoke_tests();
```

`tests/main.cpp` 按字母序把每个入口叫一遍：

```cpp
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
    run_smoke_tests();
    return ::visor::testing::failure_count() == 0 ? 0 : 1;
}
```

以后每一章新增测试文件，改动固定是四处：新文件本身、`cases.hpp` 加一行声明、`main.cpp` 加一行调用、`tests/CMakeLists.txt` 的 `add_executable` 添一个文件名：

```cmake
# Single verification entry: every case links into visor.tests, CTest sees one executable.
# Tests include the library the way a real consumer does, "visor/..." via visor.core's
# interface include path, with no private search path of their own.
add_executable(visor.tests main.cpp bit_tests.cpp canvas_tests.cpp diff_tests.cpp host_tests.cpp pen_tests.cpp point_tests.cpp rect_tests.cpp smoke_tests.cpp)
target_link_libraries(visor.tests PRIVATE visor.host)

add_test(NAME visor.tests COMMAND visor.tests)
```

### 冒烟测试

第一批用例从最没出息的一条开始，`tests/smoke_tests.cpp`：

```cpp
#include "cases.hpp"
#include "testing.hpp"

void run_smoke_tests() {
    VISOR_CHECK_EQ(1, 1);
}
```

断言 1 等于 1，不验证任何数学。它验证的是链路本身：宏能展开，失败能计数，打印能到达终端，`main` 能返回正确的退出码。链路先绿，后面每一章的真断言才有资格谈红与绿。

## 格式检查

格式规则在 `.clang-format`，4 空格缩进、100 列、大括号附着在行尾、指针星号靠类型（`int* a`）。检查由一个自定义目标承担：

```cmake
file(GLOB_RECURSE VISOR_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/include/*.hpp
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_custom_target(visor.check-format
    COMMAND ${VISOR_CLANG_FORMAT} --dry-run --Werror ${VISOR_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format 22.1.8 检查格式")
```

`--dry-run` 意味着不改任何文件，只报不平的地方，`--Werror` 把“报了”升级为“退出非零”。格式不合在任何一台版本正确的机器上都过不了这关，代码风格从此不靠人眼评审。

## 静态检查

`.clang-tidy` 定检查集与命名规则，`visor.check-tidy` 目标执行它：

```cmake
file(GLOB_RECURSE VISOR_TIDY_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_custom_target(visor.check-tidy
    COMMAND ${VISOR_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR} ${VISOR_TIDY_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-tidy 22.1.8 静态检查")
```

tidy 只直接跑 `.cpp` 文件，头文件经包含受查。`-p` 指向构建目录里的 `compile_commands.json`，tidy 拿到与真实构建一致的编译命令，宏与包含路径都不会猜错。

检查集本体：

```yaml
Checks: >
  -*,
  bugprone-*,
  cert-*,
  misc-*,
  modernize-*,
  performance-*,
  portability-*,
  readability-*,
  -bugprone-easily-swappable-parameters,
  -misc-non-private-member-variables-in-classes,
  -modernize-use-trailing-return-type,
  -portability-avoid-pragma-once,
  -readability-function-cognitive-complexity,
  -readability-identifier-length,
  -readability-magic-numbers
WarningsAsErrors: '*'
HeaderFilterRegex: 'visor/'
FormatStyle: file
```

基础集开到七个家族，警告一律当错误。排除项每一条都有具体的理由：

| 排除项 | 理由 |
| --- | --- |
| `bugprone-easily-swappable-parameters` | 像素坐标 `x, y` 与 `Rect` 的字段天然同名可换，这类“可换”是领域事实，不是缺陷 |
| `misc-non-private-member-variables-in-classes` | 聚合结构体（`Point`、`Rect`）的成员就是公开数据，这是设计 |
| `modernize-use-trailing-return-type` | 不采用尾置返回类型，写法保持传统 |
| `portability-avoid-pragma-once` | 全仓头文件统一用 `#pragma once`，教学仓不引入包含保护宏的噪音 |
| `readability-function-cognitive-complexity` | 认知复杂度对教学代码太吵，测试函数尤其爱超 |
| `readability-identifier-length` | `x`、`y` 这类短名是坐标的正当名字 |
| `readability-magic-numbers` | 位操作库满篇 `0xF8`、`0x80`，这些数字本身就是教学内容 |

命名规则写在 `CheckOptions` 里：类型、结构体、枚举、Concept 一律 `PascalCase`，函数、变量、参数、成员、命名空间一律 `snake_case`，私有成员尾下划线，常量 `kPascalCase`（比如你将见到的 `kStrideBytes`），枚举值 `PascalCase`（比如 `Color::On`）。命名从此也是机器检查的一部分，风格问题不需要讨论。

tidy 在后面几章还会亲自上课，第二章你会看到它教我们指名初始化的分界线。

## 文档基建

API 文档用 Doxygen 生成，`visor.docs` 目标手动触发，不进默认构建：

```cmake
add_custom_target(visor.docs
    COMMAND ${VISOR_DOXYGEN} ${CMAKE_SOURCE_DIR}/document/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Doxygen 生成 API 文档到 document/api")
```

`document/Doxyfile` 的关键项现状是：`INPUT = include README.md`，`OUTPUT_DIRECTORY = document/api`（`.gitignore` 已排除这个目录），`HAVE_DOT = YES` 借 graphviz（图形绘制工具）出类图，`EXTRACT_ALL = YES`，`WARN_IF_UNDOCUMENTED = NO`。最后一项是有意留的：头文件的 Doxygen 注释随章节逐步补齐，等铺开以后再翻成 YES，让缺失文档重新变成告警。相对路径按 `visor.docs` 目标的工作目录（仓库根）解析，配置文件本身住在 `document/` 不影响这一点。

## 取模工具的独立目录

取模工具（把汉字与图片转成点阵数组的配套件）与 GUI 核心分开推进，这是路线里定下的。所以它在 `tools/fontgen/` 有一个完全独立的构建：

```cmake
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
```

```cpp
#include <print>

// Placeholder entry until the conversion batch lands: it proves the tool configures,
// builds and runs on its own, with no dependency on the GUI core.
int main() {
    std::println("visor.fontgen 0.0.1");
    return 0;
}
```

现在它只打印一行版本号。占位也有占位的验收：这个目录能独立 configure、build、过自己的冒烟测试，并且顶层与各子目录的 CMake 对它零引用。转换逻辑属于以后的批次，目录的独立性属于今天。

## 验收

跟着走完这一章，你的仓库应该能用一条命令链让全部检查通过。在仓库根目录执行，其中 `-G Ninja` 选用 Ninja 作为构建执行器（负责实际调度编译动作的后端）：

```bash
cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/rel
ctest --test-dir build/rel --output-on-failure
cmake --build build/rel --target visor.check-format
cmake --build build/rel --target visor.check-tidy
cmake --build build/rel --target visor.docs
```

逐条核对可观察的结果：

1. configure 正常结束。如果你的 clang-format、clang-tidy 或 Doxygen 版本不对，这一步就会以中文的 FATAL_ERROR 停下，告诉你读到了什么版本。
2. 构建零报错，`-Werror` 下连警告都没有。
3. CTest（CMake 自带的测试运行器）报 `visor.tests` 通过，`100% tests passed`。此刻只有一个冒烟用例，但它走通了断言、计数、退出码整条链。
4. `visor.check-format` 与 `visor.check-tidy` 退出码为 0。
5. `document/api/html/index.html` 生成。

再验取模目录的独立性：

```bash
cmake -S tools/fontgen -B build/fontgen -G Ninja
cmake --build build/fontgen
ctest --test-dir build/fontgen --output-on-failure
grep -rn fontgen CMakeLists.txt src/ tests/
```

`fontgen.smoke` 通过，grep 零匹配。零匹配就是证据：顶层构建对取模工具没有任何依赖方向上的牵连。

有一件事照实交代：仓库此刻没有任何 git 提交，B1 的全部成果都悬在工作区，什么时候提交进仓库由作者定。所以本系列不带你做提交，验收以命令链为准。

下一章我们写第一批真正的代码：几何词汇表，`Point`、`Size`、`Rect`。GUI 后面的一切对话都用这套词汇，而它的半开区间语义里有条试金石用例等着我们。

## 参考

- [B1 实施计划：构建骨架、第一章画布与矩形、取模模块目录][plan]
- [B1 批记（2026-10-07）][note]
- [visor 工具链与基建名册][toolchain]
- [第二章 几何词汇表][next]

[plan]: ../../.claude/plans/2026-10-07-b1-build-skeleton-and-chapter1.md
[note]: ../../.claude/notes/2026-10-07-b1.md
[toolchain]: ../toolchain.md
[next]: 02-geometry.md

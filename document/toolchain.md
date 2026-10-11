# visor 工具链与基建名册（2026-10-07）

本文件把 visor 的全部基建拢成一份名册，每项记版本、角色、调用入口和强制时机。版本固定是规格要求（起草指导语言行：固定并验证各宿主工具链，不能只写标准版本）。B1 只覆盖 PC 侧，MCU 工具链（libembedded 侧记录为 arm-none-eabi-gcc 16.2.0）与 Cinux 工具链到对应梯队再入册。

## 一、B1 已入计划（九件）

| 工具 | 版本（2026-10-07 本机实测） | 角色 | 入口 | 何时强制 |
| --- | --- | --- | --- | --- |
| GCC | 16.2.1 | PC 主编译器，C++23 加三旗 | `cmake -DCMAKE_CXX_COMPILER=g++` | 每次构建 |
| CMake | 4.4.3 | 构建系统 | `cmake -S . -B build/rel -G Ninja` | 每次配置，下限 3.28 |
| Ninja | 1.13.2 | 构建后端 | 同上 `-G Ninja` | 每次构建 |
| CTest | 4.4.3 | 统一验证入口 | `ctest --test-dir build/rel` | 每任务，任务 7 加 asan 目录双跑 |
| clang-format | 22.1.8 | 代码格式 | `visor.check-format` 目标 | configure 校验版本，每任务提交前过 |
| clang-tidy | 22.1.8 | 静态检查与命名规则 | `visor.check-tidy` 目标 | 每任务提交前过，警告即错误 |
| Doxygen | 1.18.0 | API 文档 | `visor.docs` 目标 | 手动触发，configure 校验版本 |
| graphviz | 16.1.0 | 文档类图 | Doxyfile `HAVE_DOT = YES` | 随 visor.docs |
| GCC ASan/UBSan | 随 GCC | 内存与未定义行为消毒器 | `-DVISOR_SANITIZE=ON` | 批末关卡 |

配套文件与出处：

- `.clang-format`：原样复制自 `~/libembedded/.clang-format`（4 空格、100 列、附着式大括号、指针靠类型）。
- `.clang-tidy`：B1 计划任务 3 的检查集与命名规则（`kPascalCase` 常量、尾下划线私有成员进 identifier-naming 检查项）。
- `document/Doxyfile`：只吃 `include/` 与 `README.md`，产物 `document/api/` 不入库。
- `tests/testing.hpp`：手写微型断言，禁异常下不抛不终止，零第三方依赖。
- `compile_commands.json`：`CMAKE_EXPORT_COMPILE_COMMANDS ON` 导出，喂 clang-tidy 与 clangd。

## 二、在场、B1 未入册（按梯队或需要再入）

| 工具 | 版本（本机实测） | 建议入册时机 |
| --- | --- | --- |
| clangd | 22.1.8 | 编辑期语言服务（VSCode 跳转与诊断）。compile_commands 已导出，编辑器自动可用，需要 `.clangd` 配置文件时再入册 |
| Clang 编译器 | 22.1.8 | 双编译器互查可选件，B1 不强制，RGB565 光栅章或彩屏探针前再评估 |
| ccache | 4.14 | 重建加速。B1 代码量小暂缓，取模工具批量大转换实验时再入 |
| Renode | 1.17.0 | 梯队 2 OLED 实板前的仿真验证（libembedded 有 SSD1306 模型与 PNG 输出可取材） |
| QEMU | 11.1.1 | 梯队 4 Cinux 宿主验证 |

## 三、版本固定策略

configure 时硬卡三个：clang-format 22.1.8、clang-tidy 22.1.8、Doxygen 1.18.0，不符即 FATAL_ERROR（B1 计划任务 2、3）。GCC、CMake、Ninja、CTest 的版本记录在本文件与批记，不设 configure 硬卡，理由是 GCC 16 系列内的小版本差异不影响 B1 的验证口径，到 MCU 工具链入册时一并定死各自的锁法。

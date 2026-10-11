# visor

取模工具 + 跨宿主 GUI 框架（AELS 工程清单的第 15 号，代号 A15）。同一份 GUI 核心按构建配置运行于四类环境：裸机 MCU 加单色 OLED、裸机 MCU 加 SPI 彩屏、Cinux（作者的 x86-64 教学操作系统）、桌面宿主。语言 C++23，禁异常、禁 RTTI，全程零第三方依赖。配套的取模工具把汉字与图片转成点阵数组，最终工具界面用本框架自己写。

这是一个从零教学工程：不 fork 现成仓库当起点，正文就是框架从零搭起来的过程本身，配[中文跟跑教程][tutorials]。仓库按批次推进，每批测试全部通过才算完成，完成即写批记。

## 适不适合你

visor 面向想看一个跨宿主 GUI 框架如何从零搭起来的读者。想直接拿一个能用的 GUI 库，成熟方案更多，visor 的价值在每一层为什么这样设计。读教程需要 C++ 基本语法与命令行经验，不需要 GUI 或嵌入式背景。

## 当前进度

第一批次（B1）已完成（2026-10-07），内容有：

- 构建与验证骨架：CMake、CTest 统一入口、clang-format 与 clang-tidy 检查目标、Doxygen 文档目标，三个工具在 configure 时锁版本。
- 几何词汇表：`Point`/`Size`/`Rect` 模板家族，矩形半开区间语义 `[x, x+w) × [y, y+h)`。
- 位操作库：`BitRange`/`BitMask`/`Bitmap`，自 Cinux 移植并修掉两处移植问题。
- 1bpp 画布：行主序、MSB 在前、填充位恒零的位布局。
- `Pen` 绘制原语：`plot` 与 `fill`。
- 宿主表第一版（仅显示提交 flush）与 PC 离屏宿主，PBM 图片导出。
- 像素差分关卡：五矩形场景三轮比对，普通构建与 ASan/UBSan 构建都通过，产出项目首张留存下来的 PBM 图片。
- `tools/fontgen` 取模工具独立目录（占位，转换逻辑属于后续批次）。

git 首提交于 2026-10-11 完成，内容为 B1 全部成果加上收尾评审的四处小改（`RectF`/`RectD` 别名改正、`Arithmetictable` 改名 `Arithmetic`、概念头注释英语修正、补 `bool` 排除的静态断言）。

以下是计划，尚未实现：事件与时钟进宿主表、脏区域收集与合并、控件树与焦点、RGB565 光栅 profile、三桌面窗口宿主（Linux、Windows、macOS）、真实 OLED 开发板、小 RAM 彩屏分块、fontgen 转换逻辑与工具界面。

## 构建与验证

需要先备好名册上的工具链（GCC 16.2.1、CMake 4.4.3、Ninja 1.13.2、clang-format 22.1.8、clang-tidy 22.1.8、Doxygen 1.18.0），其中三个工具版本在 configure 时硬卡，不符会直接报错。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

按需加跑：

```bash
cmake --build build --target visor.check-format   # 格式检查
cmake --build build --target visor.check-tidy     # 静态检查
cmake --build build --target visor.docs           # API 文档到 document/api/
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_SANITIZE=ON
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure   # ASan/UBSan
cmake -S tools/fontgen -B build-fontgen -G Ninja && cmake --build build-fontgen && ctest --test-dir build-fontgen   # 取模工具独立构建
```

每批收尾的必跑验收是一条命令，五道检查（rel 测试、ASan/UBSan 测试、format、tidy、fontgen 独立构建）任何一步失败即停，说明见 [scripts/checks/README.md](scripts/checks/README.md)：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

## 文档地图

公开文档：

- [跟跑教程][tutorials]：从空仓库到差分关卡的七章正文，入口导读在[教程目录][tutorials-index]。
- [工具链与基建名册][toolchain]：全部工具的版本、角色与强制时机。
- 开发者面向的工作流（分工、提交口径、验收与架构硬边界）在 [CLAUDE.md](CLAUDE.md)，必跑验收说明在 [scripts/checks/README.md](scripts/checks/README.md)。

API 文档不入库，`visor.docs` 目标生成到 `document/api/`。

[tutorials]: document/tutorials/01-skeleton.md
[tutorials-index]: document/tutorials/README.md
[brief]: .claude/spec/aels-a15-gui-brief.md
[review]: .claude/spec/aels-a15-review-2026-10-07.md
[plan]: .claude/plans/2026-10-07-b1-build-skeleton-and-chapter1.md
[note]: .claude/notes/2026-10-07-b1.md
[toolchain]: document/toolchain.md

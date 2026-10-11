# 第五章 笔：plot 与 fill，以及越界为什么静默

画布有了，这一章给它配笔。B1 计划里原本的方案是一个 `fill_rect` 自由函数，实现阶段作者提议改成了笔：一支笔绑住一块画布、带一种墨色，笔画出点与面。改动的理由值得讲，因为它决定了以后所有绘制原语的形状。这一章结束时 `pen` 全部用例通过，测试里的六支笔全部是 `const` 的，这件事本身就藏着本章的核心设计。

笔住在 `include/visor/draw/pen.hpp`，命名空间 `visor::draw`。

## 为什么是笔，不是函数

`fill_rect(canvas, rect, color)` 这样的函数每次调用都要把墨色递一遍，绘制状态散落在每个调用点。笔把墨色收进对象，蘸一次墨，画很多笔。B1 这批的笔只有两级原语，`plot` 画一个点，`fill` 涂一个面。图案、线段、混色、裁剪栈都不在本批，每批只进一个机制。

整份头文件不长，全文贴出来，每行注释都有用：

```cpp
#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
// rect_helper.hpp provides intersect, found through ADL on the Rect arguments;
// include-cleaner cannot attribute ADL hits, so the keep pragma stands in.
#include "visor/base/helpers/rect_helper.hpp" // IWYU pragma: keep

namespace visor::draw {

/**
 * @brief A drawing hand bound to one canvas: carries the ink state, provides the strokes.
 *
 * Templates on the canvas type, not on how the canvas is parameterized, so a profile
 * parameter added in the RGB565 chapter never touches the Pen. Requirement on CanvasT:
 * exposes kWidth, kHeight and set(PointI, Ink). Ch1 ink is one Color; later chapters
 * grow the state without changing the call sites. A pen is meant to be short-lived,
 * created per draw call beside the canvas it borrows.
 */
template <typename CanvasT, typename Ink>
class Pen {
  public:
    constexpr Pen(CanvasT& target, Ink ink) noexcept : canvas_{target}, ink_{ink} {}

    /// Paints one pixel; an out-of-bounds plot vanishes silently. Const: the pen
    /// never changes, the canvas it points at does.
    constexpr void plot(base::PointI p) const noexcept {
        // set reports out-of-bounds with its return value; drawing ignores that on
        // purpose, stroke algorithms must not check bounds at every step.
        static_cast<void>(canvas_.set(p, ink_));
    }

    /// Fills the part of area that lands on the canvas, nothing outside.
    constexpr void fill(base::RectI area) const noexcept {
        const base::RectI visible = base::intersect(
            area, base::RectI{.pos = base::PointI{0, 0},
                              .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                                  .h = static_cast<int>(CanvasT::kHeight)}});
        if (visible.is_empty()) {
            return;
        }
        const int right = visible.pos.x + visible.size.w;
        const int bottom = visible.pos.y + visible.size.h;
        for (int y = visible.pos.y; y < bottom; ++y) {
            for (int x = visible.pos.x; x < right; ++x) {
                plot(base::PointI{x, y});
            }
        }
    }

  private:
    CanvasT& canvas_;
    Ink ink_;
};

} // namespace visor::draw
```

开头那条 `IWYU pragma: keep` 的注释是一个真实的工程细节：`intersect` 是通过 ADL（argument-dependent lookup，实参相依查找，编译器在实参的命名空间里找函数）找到的，包含清理工具认不出这种命中，会报“这个 include 没用”，所以用 pragma 声明这个包含是有意的。工具的短视用注释补，不迁就工具删掉正确的包含。

## 模板挂在画布类型上，给 RGB565 留缝

`Pen` 的模板参数只有一个 `typename CanvasT`，一个画布类型。尺寸整数在这里不出现，注释里写明了为什么：计划中的 RGB565 章节（尚未成文）给画布加 profile 参数时，改动落在 `Canvas` 的模板签名上，`Pen` 一个字都不用动。画布从 `Canvas<SizeI>` 扩展成 `Canvas<SizeI, Profile>`，笔照旧工作，因为笔只要求画布“暴露 `kWidth`、`kHeight` 和 `set(PointI, Ink)`”。这份要求现在是注释里的白纸黑字，第二个 profile 进场时把它升格成真正的 Concept（编译期检查的正式形态），那是教 Concepts 的正牌时机，属于计划中的批次。

`Ink` 也是模板参数。B1 批次的墨是 `Color`，以后一支笔可以带更多状态（字模、混色参数），携带状态的是笔，画布与调用点不用跟着变。

测试里的笔从来不写模板参数，靠 CTAD（class template argument deduction，类模板实参推导，从构造实参反推模板参数）：

```cpp
    const Pen pen{single, Color::On};
```

## 越界为什么静默

`plot` 里最扎眼的一行是 `static_cast<void>(canvas_.set(p, ink_))`，画布明明返回了越界与否，笔故意把返回值扔了。注释原文给了理由：笔画算法不该在每一步查边界。

边界处理在整条链上各有其位。画布的 `set` 是最后防线，越界拒绝写、一字节不动（第四章验过）。笔的 `fill` 在进入逐像素循环之前先做一次裁剪，把要涂的区域与画布全幅求交，交空直接返回。有了这两层，`plot` 里的每一次边界检查都是重复劳动：直线、圆弧这类逐点推进的笔画算法，每个点都查一遍界，热的路径上全是废指令。静默的设计把边界责任放到了它该在的层。

扔返回值用 `static_cast<void>` 明说“我知道有返回值，我就是要扔”，比 C 风格的 `(void)` 转换诚实，翻代码的人不用猜这是不是手滑。

## const 笔语义：笔不变，纸变

`plot` 与 `fill` 都是 `const` 成员函数。乍看矛盾：const 的东西怎么能画？

我们把私有的两个成员翻出来看就通了：`canvas_` 是引用，`ink_` 是值。画一个像素改的是引用指向的画布，笔自己的两个成员从头到尾没动。const 约束管的是笔，不管笔指着的世界。于是绘制代码可以这样写：

```cpp
    const Pen clip_brush{clipped, Color::On};
    clip_brush.fill((RectI{.pos = PointI{-10, -10}, .size = SizeI{.w = 30, .h = 30}}));
```

笔一旦落地就是只读的配置，墨色不会再变。测试里六支笔全部声明成 `const`，没有一支需要可变。这个设计还带出一个纪律：笔的生命周期短，注释里写明“created per draw call beside the canvas it borrows”，谁借的画布谁在场，画完笔就亡，不存在一支笔活过它的画布的悬垂引用。成员是引用的类无法赋值，真写出这种代码，编译器直接拦住，连编译都过不了。

## 测试逐组看

`tests/pen_tests.cpp` 按行为一组组过。

单点与字节对应关系：

```cpp
    // plot lights exactly one pixel: pixel (3, 4) is bit 0x10 of row 4's first byte.
    TestCanvas single;
    const Pen pen{single, Color::On};
    pen.plot(PointI{3, 4});
    VISOR_CHECK_EQ(single.get((PointI{3, 4})), Color::On);
    VISOR_CHECK_EQ(single.pixels()[(4 * 4) + 0], std::uint8_t{0x10});
```

测试把 32×32 的画布起名 `TestCanvas`。我们跟着算一遍：每行 4 字节，第 4 行首字节下标 `4*4`，像素 3 的位是 `0x80 >> 3` 即 `0x10`。字面断言又一次直接检验了 MSB 在前。

越界静默，用快照验：

```cpp
    // Out-of-bounds plots vanish silently, the buffer stays byte-identical.
    const TestCanvas before = single;
    pen.plot((PointI{-1, 0}));
    pen.plot((PointI{32, 0}));
    pen.plot((PointI{0, 32}));
    pen.plot((PointI{0, -1}));
    for (std::size_t i = 0; i < before.pixels().size(); ++i) {
        VISOR_CHECK_EQ(single.pixels()[i], before.pixels()[i]);
    }
```

四个方向的越界各点一笔，整个缓冲逐字节比对。静默的承诺是“什么都没发生”，那就逐字节验证什么都没发生。

裁剪用例是最重的一条，负原点矩形涂 32×32 画布：

```cpp
    // A negative-origin area clips to the canvas: exactly [0,20) x [0,20) lights up,
    // verified pixel by pixel, not only at the corners.
    TestCanvas clipped;
    const Pen clip_brush{clipped, Color::On};
    clip_brush.fill((RectI{.pos = PointI{-10, -10}, .size = SizeI{.w = 30, .h = 30}}));
    std::size_t lit_count = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool expect_lit = x < 20 && y < 20;
            VISOR_CHECK_EQ(clipped.get((PointI{x, y})), expect_lit ? Color::On : Color::Off);
            if (expect_lit) {
                ++lit_count;
            }
        }
    }
    VISOR_CHECK_EQ(lit_count, std::size_t{400});
```

`{-10,-10}` 起步 30×30 的面，我们与画布求交，得到恰好 `[0,20)×[0,20)`，20 乘 20，正是断言数出的那 400 个亮点。1024 个像素逐个验。注释特意说明“verified pixel by pixel, not only at the corners”：只查四个角，区域中间漏掉的部分就发现不了。

退化矩形与全出界，继续快照法：

```cpp
    // A fully outside area and degenerate areas are no-ops.
    const TestCanvas untouched = clipped;
    clip_brush.fill((RectI{.pos = PointI{130, 10}, .size = SizeI{.w = 5, .h = 5}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 0, .h = 5}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 5, .h = 0}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = -3, .h = 5}}));
    for (std::size_t i = 0; i < untouched.pixels().size(); ++i) {
        VISOR_CHECK_EQ(clipped.pixels()[i], untouched.pixels()[i]);
    }
```

全出界、宽零、高零、负宽，四种退化各涂一次，字节一个不许变。负宽不翻转、不炸，交完就是空，这是第二章 `is_empty` 语义的直接受益。

奇宽画布（测试把 13×5 的画布起名 `OddCanvas`）上的 3 像素面：

```cpp
    // A 3-wide fill starting at pixel 1 sets bits 0x40, 0x20 and 0x10 of the first byte.
    OddCanvas odd;
    const Pen odd_brush{odd, Color::On};
    odd_brush.fill((RectI{.pos = PointI{1, 0}, .size = SizeI{.w = 3, .h = 1}}));
    VISOR_CHECK_EQ(odd.pixels()[0], std::uint8_t{0x70});
```

覆盖像素 1、2、3，我们把三个位加起来：`0x40`、`0x20`、`0x10`，合成 `0x70`。整幅填充与 `set_clear(On)` 逐字节相同、`Off` 墨的笔把亮区涂灭，这两组对照测试在文件末尾，思路同前，不再贴。

## 验收

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`pen` 全部用例通过。

做一次移位实验。编辑 `tests/pen_tests.cpp`，把 `0x70` 用例的起点从 `x = 1` 挪到 `x = 0`：

```cpp
    odd_brush.fill((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 3, .h = 1}}));
```

覆盖像素变成 0、1、2，先心算新的首字节值，把它填进断言替换 `0x70`，跑 `ctest --test-dir build --output-on-failure`。算对了绿，算错了 FAIL 行等着你。实验完改回去。

下一章走出核心：宿主表，GUI 核心与外部世界的全部边界。核心画、宿主收，第一版表里只有 flush 一项，外加一个住在 PC 内存里的假屏幕和它的 PBM 导出。

## 参考

- [B1 批记（2026-10-07）：Pen 由 fill_rect 改制的记录][note]
- [第四章 画布][prev]
- [第六章 宿主表与离屏宿主][next]

[note]: ../../.claude/notes/2026-10-07-b1.md
[prev]: 04-canvas.md
[next]: 06-host.md

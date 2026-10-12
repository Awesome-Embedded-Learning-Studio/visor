# 第十五章 位图原语与字形渲染

路线拐了个弯：SSD1306 的驱动适配交给那边专门的线，我们朝 Cinux 走。可交叉清单里有条硬前置——第四站开工前字体与文本必须完成，而 visor 至今画不出一个字符。B7 铺好了供给线，fontgen 能把图转成头文件，但头文件躺在仓库里不会自己发光。这一批做消费侧：绘制核心长出位图原语，字形就是它的第一种食材。

## MonoBitmap：不拥有数据的视图

位图在核心眼里的形状，是画布打包的只读视图：

```cpp
struct MonoBitmap {
    std::size_t width = 0;
    std::size_t height = 0;
    std::span<const std::uint8_t> rows;

    /// Bytes per row in the canvas packing: (width + 7) / 8.
    [[nodiscard]] constexpr std::size_t stride() const noexcept { return (width + 7) / 8; }

    /// Reads one pixel; out-of-range reads as off. The bit position rides
    /// canvas::pixel_bit_position, the one home of that formula.
    [[nodiscard]] constexpr bool pixel(std::size_t x, std::size_t y) const noexcept {
        if (x >= width || y >= height || rows.empty()) {
            return false;
        }
        const base::bit::BitMask<std::uint8_t> word{rows[(y * stride()) + (x / 8)]};
        return word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)));
    }
};
```

三个字段，一个派生，一个读取。打包与画布严格同款（行主序、MSB 在前、填充位恒零），这是 B7 定输出格式时埋好的对接口。于是 fontgen 生成的头文件拿来就是视图的存储，一份不拷、一个字节不重排：

```cpp
constexpr MonoBitmap kChevron{
    .width = assets::chevron::kWidth,
    .height = assets::chevron::kHeight,
    .rows = assets::chevron::kBits,
};
```

生成头里的 `kStrideBytes` 在视图这边没有对应字段，stride 是派生的。这个「冗余」留着有用：它是两边口径的校验点，一条 `static_assert(assets::chevron::kStrideBytes == kChevron.stride())` 让漂移在编译期就死掉。本批判例把这个组装整个写了一遍，连生成头带行注释的样子都照抄——判例测的就是「生成头拿来即用」这条路本身。

视图不拥有数据，所以它指向的东西活得比它久。判例里 8x8 对角线字模的行数组先落成命名常量、视图再指过去——直接在聚合初始化里写临时数组，span 指向的对象在初始化结束就没了，程序还没跑起来视图已经悬空。constexpr 不会替你查这个。

## 原语：墨水传送，不是区域覆盖

`Painter` 长出第三个动词：

```cpp
/// Blits a packed 1bpp bitmap: every lit source pixel becomes a scale x
/// scale block of ink, dark pixels paint nothing (this is ink transfer,
/// not area overwrite — erasing around a bitmap is the caller's fill).
/// Parts outside the frame clip silently. scale < 1 clamps to 1. The
/// scale is explicit at every call site on purpose: a default argument on
/// a virtual would bind to the static type's declaration, so the seam
/// keeps none.
virtual void draw_bitmap(base::PointI at, const draw::MonoBitmap& source,
                         canvas::Color ink, int scale) const noexcept = 0;
```

语义只有一条要记住：灭的源像素什么都不画。这是墨水传送，把点阵里亮着的位搬成墨水，不碰别处。想在字底下垫一块纯色背景，先自己 `fill` 那块矩形，再画字。为什么不做「区域覆盖」版本（灭位写底色）？因为那是两个参数的语义（前景墨、背景墨），等真有控件需要它再进场，第一版原语只扛一件事。判例里有一条专门守这个：画布预填全亮，用 Off 墨画对角线字模，字模的灭位区域保持原样，只有亮位的那条线被「擦」下去。

`scale` 没有默认值，这不是省事，是被编译器教的。第一版在基类声明上写了 `int scale = 1`，判例里 `painter.draw_bitmap(at, glyph, ink)` 三参调用直接编译不过——painter 那个名字的静态类型是 `CanvasPainter`，默认参数跟着静态类型的声明走，而 override 声明上没有默认参数。虚函数带默认参数是著名的老陷阱（通过基类引用调用用基类的默认值，通过派生类对象调用用派生类的，两边不一致时行为就分叉），让它咬自己一口比背书强。缝上不留默认值，每个调用点写明白，这事就没了。

## 缩放：位变块，块不重叠

整数缩放的实现一行讲完：每个亮的源位画一个 `scale x scale` 的墨水块，块的原点是源坐标乘 factor。前身仓 P5 那条「零新字体数据、整数倍放大」的路线原样搬过来——8x16 的字模放大两倍成 16x32，粗糙但可读，不用为每个字号备一份字模。

关键性质是相邻块不重叠：第 n 列的块横跨 `[at.x + n*scale, at.x + (n+1)*scale)`，下一块从 `(n+1)*scale` 起，恰好接壤。不重叠意味着画的顺序无所谓、同一块画两遍结果不变——这正是条带模型要的：一条 6 行的条带装不下 16 行的字，字会被多条带各画一次，每次画自己窗口里的那几行，拼起来与整幅画一遍逐像素一致。判例把三个字模（两种 scale、两种墨色）的场景整幅画一遍、按 6 行条带画三遍，256 个像素逐位对齐。`scale` 小于 1 的荒谬值钳到 1，与「输入错误不终止」同一条口径。

## 判例实录：对角线的尾巴

这一批判例红的全是自己的期望，三回都在同一件事上：对角线数步数。8 步对角线画在 (2,3)，最后一个亮位在 (9,10)，我断言 (9,10) 是「盒子外的暗角」——它是 step 7，正亮着。灭位判例里挑 (11,11) 当「盒子内暗位」，它恰恰是 step 7 的对角线点，被墨水擦下去了。窗口判例算窗口装得下几步，窗口 x 到 9 为止、字模从 5 起，装 5 步，我写了 6。三回全是实现行为正确、期望数学算错，判例的价值就在这：它不信任何人的心算。

红绿探针两根。缩放探针把块原点的乘 factor 抹掉（`at.x + column`），27 处断言红，缩放判例与条带重放里的 scale 2 场景全倒。位序探针把源位读取的 MSB 颠倒成 LSB，结果没轮到运行判例出手——`static_assert(kChevron.pixel(0, 0))` 这类编译期断言直接让构建停下。位序错在编译期就死，比探针还早一步，这算 constexpr 判例的一次意外演出。

## 验收

```console
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
$ cmake --build build
$ ctest --test-dir build --output-on-failure
1/1 Test #1: visor.tests ......................   Passed
100% tests passed
```

红绿实验两个。第一个，把 `canvas_painter.hpp` 里 `draw_bitmap` 的块原点改成不乘 factor（`.pos = base::PointI{at.x + static_cast<int>(column), at.y + static_cast<int>(row)}`），重跑看缩放与条带判例转红，复原再绿。第二个，把 `mono_bitmap.hpp` 里 `pixel` 的读取位颠倒（`7U - canvas::pixel_bit_position(x)`），这次连构建都过不去，static_assert 在编译期把位序守住，复原再绿。

## 下一批的口子

单个字会画了，一段字还排不了：一串字模各多宽、中间隔几像素、换行怎么算，这是文本测量的事，`Label` 控件靠它把自己撑到文字的大小。下一批做测量与 Label，字体与文本组的最后一件。

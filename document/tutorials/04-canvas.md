# 第四章 画布：1bpp 的位布局与一个叫 Color 的名字

第三章把位操作词汇备齐了，这一章把它们拼成一件真正的东西：画布。单色 GUI 的一切画面都落在一块 1bpp（bits per pixel，每像素一比特）的画布上，亮或不亮一个比特说了算。这一章要定三件事：墨色的名字、比特在字节里的摆法、填充位的纪律。这一章结束时 `canvas` 全部用例通过，其中有一条 `0xF8` 的字面断言，把奇数宽度的位布局锁定在字节层面。

画布住在 `include/visor/canvas/mono_canvas.hpp` 与 `include/visor/canvas/color.hpp`，命名空间 `visor::canvas`。

## 先给墨色定名：Color，不叫 MonoState

最省事的名字呼之欲出：既然每像素一比特，叫 `MonoState` 或者 `Mono` 好了。作者否掉了这条路，理由是：不把 1bpp 泄进 API 名。RGB565（每像素 16 位的彩色格式，计划中的后续 profile）进场时，每个 profile 会有自己的一份 `Color`，profile 无关的泛型代码一个名字都不用改。

定名之后的本体只有一行：

```cpp
enum class Color : std::uint8_t { Off = 0, On = 1 };
```

文件头的注释把语义说得很干净：

> The values speak about the bit, not a physical color: which shade a set bit shows is the panel's business.

`On` 说的是这一比特亮了，至于亮起来的像素在屏幕上是白是黑，那是面板的事。同一份画布数据，白底黑字的屏与黑底白字的屏读出相反的图像，画布层对此不持立场。

## 位布局：行主序，MSB 在前，填充位恒零

布局规则三条，写进类型注释里，我们逐条过。

行主序（row-major）：第一行的字节存完，再存第二行。每行 `(Width + 7) / 8` 个字节，向上取整。

MSB 在前（MSB，most significant bit，一个字节里最左边的比特）：一行的第 `x` 个像素住在这行第 `x/8` 个字节里，占据 `0x80 >> (x%8)` 那一位。也就是说最左边的像素用字节的最高位，像素 0 是 `0x80`，像素 7 是 `0x01`。

填充位恒零：宽度不是 8 的倍数时，行尾字节里装不下任何像素的位必须保持零。

三条规则在代码里的唯一住所是一个小函数：

```cpp
/// @brief Pixel x's bit position inside its byte, MSB first: pixel 0 rides the top
///        bit. The one home of the mono packing formula; hosts decode with it too.
constexpr std::uint8_t pixel_bit_position(std::size_t x) {
    return static_cast<std::uint8_t>(7 - (x % 8));
}
```

“the one home” 是字面意思：`7 - (x % 8)` 这个换算公式在全库只此一处。画布的读写调用它，第六章宿主解码收到的帧也调用它，调用者再多，公式本身没有第二份手写副本。第三章说过，这种换算不进人脑，现在它也不再进第二份代码。

为什么选 MSB 在前？因为 PBM P4（Portable Bitmap 的二进制格式）对打包行的规定与此逐位同构。画布数据可以原样写进图片文件，中间不需要任何位重排。第七章我们会用它产出项目第一张留存下来的图片。

## Canvas 本体

```cpp
template <base::SizeI CanvasSize>
class Canvas {
  public:
    static constexpr std::size_t kWidth = CanvasSize.w;
    static constexpr std::size_t kHeight = CanvasSize.h;
    static constexpr std::size_t kStrideBytes = (kWidth + 7) / 8;
```

模板参数是一个 `SizeI` 的值。这里最早的设计是 `Canvas<13, 5>` 两个裸 `int`，实现时更新成尺寸做模板实参、`set_clear` 做清屏名、坐标走 `PointI`。尺寸既然是编译期常量，行距（stride，一行占的字节数）`kStrideBytes` 与缓冲区大小也全部编译期定死：

```cpp
  private:
    std::array<std::uint8_t, kStrideBytes * kHeight> bits_{};
```

`std::array` 带初始化，容量、对齐、零初值都由类型系统管，没有动态分配。

### 清屏：灌满之后修剪行尾

```cpp
    /// Sets every pixel to state. The padding bits past the last pixel stay zero.
    constexpr void set_clear(Color state) noexcept {
        bits_.fill(state == Color::On ? base::bit::ones<std::uint8_t>(8) : std::uint8_t{0});
        if constexpr (kWidth % 8 != 0) {
            // The tail byte's padding field: the bits below the last pixel stay zero.
            const base::bit::BitRange padding{.low = 0,
                                              .width = static_cast<std::uint8_t>(8 - (kWidth % 8))};
            for (std::size_t row = 0; row < kHeight; ++row) {
                const std::size_t index = (row * kStrideBytes) + (kStrideBytes - 1);
                base::bit::BitMask<std::uint8_t> word{bits_[index]};
                word.deposit(padding, 0);
                bits_[index] = word.raw;
            }
        }
    }
```

清屏两步。整块缓冲先按墨色灌满，宽度是 8 的倍数时到此为止。不是倍数时，每行末字节里越界的填充位是一个真实存在的位段，宽度 `8 - (kWidth % 8)`，用第三章的 `deposit` 往里写零。`if constexpr` 让宽度整除 8 的画布连修剪循环都不编译，代码里也不留运行期判断。

### 读写一个像素

```cpp
    /// Writes one pixel. Out-of-bounds coordinates return false and touch nothing.
    [[nodiscard]] constexpr bool set(base::PointI p, Color value) noexcept {
        if (p.x < 0 || p.y < 0) {
            return false;
        }
        const auto x = static_cast<std::size_t>(p.x);
        const auto y = static_cast<std::size_t>(p.y);
        if (x >= kWidth || y >= kHeight) {
            return false;
        }
        const std::size_t index = (y * kStrideBytes) + (x / 8);
        base::bit::BitMask<std::uint8_t> word{bits_[index]};
        word.deposit(base::bit::bit_at(pixel_bit_position(x)), value == Color::On ? 1 : 0);
        bits_[index] = word.raw;
        return true;
    }
```

我们停下来看两道边界检查的顺序。先判 `p.x < 0`，后把 `int` 转成 `std::size_t`。负数转无符号是定义好的回绕，`-1` 会变成 `SIZE_MAX` 这样巨大的值，随后的范围检查多半也能拦住它。但把一个明明白白的负坐标放进无符号算术里走一遭，纯属没有必要：负数的问题就该在还有符号的世界里问完，无符号世界只做下标算术。这是嵌入式代码的一条基本纪律。

越界写入返回 `false` 且一字节不动，越界读取（`get`）返回 `Color::Off`。不抛、不终止、不写坏内存，这是核心层处理坏输入的统一态度。

像素写入本身全是第三章的词汇：取出所在字节当 8 位字，`bit_at(pixel_bit_position(x))` 盖出那个像素的位段，`deposit` 安放墨色，写回。

### 原始行

```cpp
    /// Raw rows for the host table's flush, kStrideBytes bytes per row, MSB first.
    [[nodiscard]] constexpr std::span<const std::uint8_t> pixels() const noexcept { return bits_; }
    [[nodiscard]] constexpr std::span<std::uint8_t> pixels() noexcept { return bits_; }
```

两个重载，一个只读一个可写，都是整块缓冲的视图。第六章宿主表的 flush 靠它拿到像素行，测试也靠它做逐字节断言。

## 0xF8：奇数宽度的字面断言

测试取了宽度光谱的两端：8（整字节）与 13（余 5）。

```cpp
    // 8 fits a byte per row, 13 needs padding: the two ends of the packing story.
    using ByteAlignedCanvas = Canvas<SizeI{.w = 8, .h = 2}>;
    using OddWidthCanvas = Canvas<SizeI{.w = 13, .h = 5}>;

    static_assert(ByteAlignedCanvas::kStrideBytes == 1UZ);
    static_assert(OddWidthCanvas::kStrideBytes == 2UZ);
```

行距本身是编译期断言，配错了模板都实例化不出来。整字节的清屏干净利落：

```cpp
    // A byte-aligned canvas fills whole bytes: set_clear On is all 0xFF, Off is all zeros.
    ByteAlignedCanvas aligned;
    aligned.set_clear(Color::On);
    VISOR_CHECK_EQ(aligned.pixels()[0], std::uint8_t{0xFF});
```

再看 13 宽的画布：

```cpp
    // Padding bits stay zero even after set_clear(On): on a 13-wide canvas the last byte
    // of every row is 0xF8, five pixel bits set and three padding bits clear.
    OddWidthCanvas odd;
    odd.set_clear(Color::On);
    for (std::size_t row = 0; row < OddWidthCanvas::kHeight; ++row) {
        VISOR_CHECK_EQ(odd.pixels()[row * OddWidthCanvas::kStrideBytes], std::uint8_t{0xFF});
        VISOR_CHECK_EQ(odd.pixels()[(row * OddWidthCanvas::kStrideBytes) + 1], std::uint8_t{0xF8});
    }
```

`0xF8` 是二进制 `1111_1000`。我们逐位排一遍：13 个像素，前 8 个占满第一字节 `0xFF`，后 5 个占第二字节的高 5 位，剩下 3 位是填充，恒零，于是 `0xF8`。这条断言检验的是字面值：测试注释里那句“填充位为零”是翻译过的描述，断言认的是那个具体的字节。任何实现改动只要让填充位亮起来、或者让像素位错了位，这条断言立刻转红。

单像素的写入也一样字面：

```cpp
    // Setting the origin touches exactly the top bit of the first byte.
    OddWidthCanvas single;
    VISOR_CHECK(single.set((PointI{0, 0}), Color::On));
    VISOR_CHECK_EQ(single.pixels()[0], std::uint8_t{0x80});
```

MSB 在前的直接后果：像素 0 是 `0x80`，不是 `0x01`。

越界部分测试做了一次快照对比，写坏一个字节都逃不掉：

```cpp
    // Out-of-bounds coordinates: set refuses and touches nothing, get reads Off.
    OddWidthCanvas guard;
    VISOR_CHECK(guard.set((PointI{5, 2}), Color::On));
    const OddWidthCanvas snapshot = guard;
    VISOR_CHECK(!guard.set((PointI{-1, 0}), Color::On));
    VISOR_CHECK(!guard.set((PointI{0, -1}), Color::On));
    VISOR_CHECK(!guard.set((PointI{13, 0}), Color::On));
    VISOR_CHECK(!guard.set((PointI{0, 5}), Color::On));
    for (std::size_t i = 0; i < snapshot.pixels().size(); ++i) {
        VISOR_CHECK_EQ(guard.pixels()[i], snapshot.pixels()[i]);
    }
```

四类越界（负 x、负 y、超宽、超高）各来一次，然后整个缓冲逐字节比对快照。拒绝写不等于没写，只有字节一个没变才算数。

还有一条注释值得念出声，类型注释的最后一行：

> This is the canvas-internal format, not any display hardware's page format.

这是画布自己的格式。SSD1306 一类的单色 OLED 面板内部按页（page，8 行一组的竖条）组织显存，与这里的行主序完全两回事。画布故意不模仿任何硬件。把行主序数据转成面板要求的页格式属于宿主层职责，落在计划中的真实 OLED 开发板批次，本仓尚未实现。

## 验收

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`canvas` 全部用例通过。

做一次换宽实验，亲手验证你对位布局的掌握。编辑 `tests/canvas_tests.cpp`，把 `OddWidthCanvas` 的宽度从 13 改成 12：

```cpp
    using OddWidthCanvas = Canvas<SizeI{.w = 12, .h = 5}>;
```

先推演再跑：12 个像素，每行末字节变成 4 个像素位加 4 个填充位，`0xF8` 的断言会失败，实际值应该是多少？`(12, 4)` 那次 `set` 还会成功吗？跑一遍 `ctest --test-dir build --output-on-failure`，数一数红了几条断言，与你的推演对答案。实验完把 13 改回去。

下一章给画布配笔。plot 与 fill 两级原语、越界静默的设计理由、还有一支 const 笔为什么照样能画。

# 第六章 宿主表与离屏宿主：核心与外界的边界只此一张表

前五章都在核心里，这一章走到边界上。GUI 核心自己是画布上的几何与像素，屏幕、窗口、面板这些形形色色的外部世界，对核心来说全部浓缩成一张宿主表：核心画，宿主收。第一版的表里只有显示提交一项，配上第一个宿主，一个住在 PC 内存里的假屏幕，外加 PBM 图片导出。这一章结束时 `host` 全部用例通过，你还能亲手产出一个能被图片查看器打开的文件。

宿主表的头文件是 `include/visor/bindings/host.hpp`，实现在 `src/bindings/host.cpp`，后者编进 `visor.host` 静态库（第一章占位的那个空文件，从这一章起有内容）。命名空间 `visor::host`。

## 方向：核心画，宿主收

宿主表的结构定下来之前，方向先定：数据的流向是核心把画好的像素交给宿主，宿主只管收。

这个方向不是随手选的。作者的前身工程仓 Cinux-GUI 里也有一张宿主表，方向正相反：宿主侧通过 `render_frame` 回调把场景画进核心的暂存缓冲。这个 Cinux-GUI 与第三章位操作库的来源 Cinux 是两个仓，前者是 2026 年 7 月停更的 GUI 工程，后者是教学操作系统。两代工程的表方向正相反，立项时特意把这条差异写明。不写明的话，顺着旧表的形状走，很容易把渲染回调原样带回来。visor 的架构要求是控件渲染留在核心，宿主只交显示提交、输入事件与时钟。表的样子可以慢慢补，方向第一天就不能错。

flush 是默认的显示模型：核心管理受预算约束的暂存存储，按区域提交像素。零拷贝、直接绘制到后备帧缓冲与交换缓冲都是正确的备选路线，全部留作后续扩展，第一版只做 flush。

## HostTable：一张表，一个入口

```cpp
/// @brief Pixel layouts a FlushRegion can carry. Ch1 has exactly one.
enum class PixelFormat : std::uint8_t { Mono1 };

/**
 * @brief One submission of pixels travelling from a canvas to a host.
 *
 * rect is half-open in canvas coordinates, pixels points at the first byte of
 * rect's top row, and stride_bytes is the hop from one row down to the next.
 * The mono layout matches the canvas packing: row-major, MSB first,
 * (width+7)/8 bytes per row.
 */
struct FlushRegion {
    base::RectI rect;
    const std::uint8_t* pixels = nullptr;
    std::size_t stride_bytes = 0;
    PixelFormat format = PixelFormat::Mono1;
};

/**
 * @brief The one boundary between the core and the world: hosts fill it in, the
 *        core only ever calls through it. Ch1 carries a single entry, the
 *        display flush; ch2 grows event and clock entries.
 */
struct HostTable {
    void (*flush)(void* context, const FlushRegion& region) noexcept = nullptr;
    void* context = nullptr;
};
```

`FlushRegion` 是一次提交的完整自述：矩形（画布坐标系，半开，第二章的语义）、像素指针（指向矩形顶行第一个字节）、行距（从一行跳到下一行跨多少字节）、像素格式。

行距单独立一个字段，是被 13 这类奇数宽度逼出来的。我们拿 13 算一遍：画布 13 像素宽，每行 2 字节，宿主自己的缓冲也按 2 字节一行排。接收方要是自作主张按“宽度除以 8”去推步进，或按自己的行距去读对方的数据，错位就从这里开始。这条规矩立项时就写明了：宿主按 stride 步进拷贝，不按宽度假定。

`HostTable` 只有两个成员：一个函数指针，一个不透明的 `context`。这是 C 风格回调的最小形态，任何宿主都填得动。回调标了 `noexcept`，整个项目禁异常，跨边界的调用尤其不能抛。注释也把成长路线说了：第一版只带显示提交一项，事件与时钟是后续批次的事。注释里的 Ch1、ch2 按规格章节表计数，Ch1 对应的就是 B1 这一批。

## submit_frame：整幅交接

```cpp
template <typename CanvasT>
[[nodiscard]] bool submit_frame(const HostTable& host, const CanvasT& canvas) noexcept {
    if (host.flush == nullptr) {
        return false;
    }
    const FlushRegion region{
        .rect = base::RectI{.pos = base::PointI{0, 0},
                            .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                                .h = static_cast<int>(CanvasT::kHeight)}},
        .pixels = canvas.pixels().data(),
        .stride_bytes = CanvasT::kStrideBytes};
    host.flush(host.context, region);
    return true;
}
```

B1 先交整幅：矩形是画布全幅，指针指向缓冲开头，行距就是画布自己的 `kStrideBytes`。空表（flush 指针为空）返回 `false`，一次都不碰回调，不崩、不终止。模板挂在画布类型上，与第五章的笔同一课：只要求画布“有 `kWidth`、`kHeight`、`kStrideBytes` 和 `pixels()`”，不问它怎么参数化。

## OffscreenHost：第一个宿主

`OffscreenHost` 是一块住在 PC 内存里的假屏幕，文档注释把它定位得很清楚：第一个来填表的宿主，整幅接收、统计拒收、能把自己导出成 PBM。以后的 OLED 宿主与 Cinux 宿主都从这同一个形状扩展出去：填表、收像素、留一帧。

```cpp
class OffscreenHost {
  public:
    OffscreenHost(int width, int height);

    /// The table a canvas submits to; handing this to the core is all a host is.
    /// Building the channel is not a const act: the table carries mutable access.
    [[nodiscard]] HostTable table() noexcept;

    /// Reads a received pixel; out-of-bounds reads as Off.
    [[nodiscard]] canvas::Color get(base::PointI p) const noexcept;
```

构造函数按 `(w+7)/8*h` 开一块 `std::vector` 当接收缓冲。禁动态堆的约束管核心，PC 宿主不受它管，约束按层分。

`table()` 的注释解释了它为什么不是 const 成员：交出表等于交出一条可变的通道，表里的 `context` 指向这个对象，核心将来随时可能通过它写你。交表是授权，授权当然改变状态。

### 跳板函数

表里要填的是普通函数指针，`handle_flush` 却是成员函数，中间需要一座桥：

```cpp
void OffscreenHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<OffscreenHost*>(context)->handle_flush(region);
}
```

`flush_thunk`（thunk，跳板函数，把 C 风格回调转到成员函数上的静态桥）拿到不透明的 `context`，还原成对象指针，调用真正的处理函数。`table()` 里填的就是它：

```cpp
HostTable OffscreenHost::table() noexcept {
    return HostTable{.flush = &OffscreenHost::flush_thunk, .context = this};
}
```

### 只收整幅

```cpp
void OffscreenHost::handle_flush(const FlushRegion& region) noexcept {
    const bool full_frame = region.format == PixelFormat::Mono1 && region.rect.pos.x == 0 &&
                            region.rect.pos.y == 0 && region.rect.size.w == width_ &&
                            region.rect.size.h == height_ && region.rect.size.w > 0 &&
                            region.rect.size.h > 0;
    if (!full_frame) {
        ++rejected_count_;
        return;
    }
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    for (int row = 0; row < height_; ++row) {
        const std::size_t src = static_cast<std::size_t>(row) * region.stride_bytes;
        const std::size_t dst = static_cast<std::size_t>(row) * stride;
        std::copy_n(region.pixels + src, stride, frame_.data() + dst);
    }
    ++flush_count_;
}
```

B1 的宿主只保证整幅接收。我们跟着 `handle_flush` 的清单数一遍：格式是 Mono1、原点是零、尺寸与构造尺寸完全一致、宽高为正，任何一条不满足就记一次拒收，自己的帧一字节不动。收下时按来源的 `region.stride_bytes` 步进读，按自己的 stride 写，一行 `std::copy_n`。局部的脏区提交与分块传输属于后续批次，这里先立边界。

### 读回像素

`get` 解码自己的缓冲，用的正是第四章那个“唯一住所”：

```cpp
    const base::bit::BitMask<std::uint8_t> word{frame_[(y * stride) + (x / 8)]};
    const bool on = word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)));
```

画布与宿主对“哪个比特是哪个像素”的答案永远一致，因为它们读的是同一个函数。

## PBM 导出与现代 C++ 文件 I/O

`save_pbm` 把接收到的帧写成 PBM P4（Portable Bitmap 的二进制变体，头两行文本加打包像素行）。导出函数的签名先看一眼：

```cpp
/// @brief Writes rows as a binary PBM (P4): "P4\n<w> <h>\n", then packed rows.
[[nodiscard]] bool write_pbm(std::string_view path, int width, int height,
                             std::span<const std::uint8_t> rows, std::size_t stride_bytes) noexcept;
```

路径参数是 `std::string_view`（字符串视图，不拥有数据的只读字符串引用）。作者明确要求去掉 C 传统：接口不收 `const char*`，调用方给什么字符串视图都行。

然后是实现里一处会拦住编译的地方：

```cpp
bool write_pbm(std::string_view path, int width, int height, std::span<const std::uint8_t> rows,
               std::size_t stride_bytes) noexcept {
    // fstream constructors take no string_view directly, so the path crosses an
    // explicit std::filesystem::path.
    std::ofstream out{std::filesystem::path{path}, std::ios::binary | std::ios::trunc};
    if (!out.is_open()) {
        return false;
    }
    // println's trailing newline closes the PBM header line "P4\n<w> <h>\n".
    std::println(out, "P4\n{} {}", width, height);
    const std::size_t row_bytes = (static_cast<std::size_t>(width) + 7) / 8;
    for (int row = 0; row < height; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row) * stride_bytes;
        out.write(reinterpret_cast<const char*>(rows.data() + offset),
                  static_cast<std::streamsize>(row_bytes));
    }
    return out.good();
}
```

`std::ofstream` 的构造函数不收裸 `string_view`，直接写会编译不过。标准库在“字符串路径”这件事上认的是 `std::filesystem::path`，它倒是来者不拒，`string_view` 过这一道就能进。注释把原因记在原地，下一个写文件 I/O 的人可以直接避开这个编译错误。

头部用 `std::println(out, "P4\n{} {}", width, height)` 写。我们数一下换行：格式串自带一个 `\n`，println 自己再补行尾的换行，正好凑成 PBM 要求的头两行 `P4\n<w> <h>\n`。像素行按 `row_bytes` 逐行 `write`，来源行距由参数说了算，同一个函数既能导出整幅也能导出行距不同的视图。

### 8 个字节的头

PBM 头的长度有一段小乌龙。`"P4\n13 5\n"` 逐字节数是 `P`、`4`、换行、`1`、`3`、空格、`5`、换行，一共 8 个字节。我们按 7 个字节算过一次，读回的对不上，测试当场红。字节一个一个数出来，比心里的答案多一个。后来读回断言干脆把这个事实写成字面值：

```cpp
        // The PBM header is 8 bytes: "P4\n13 5\n".
        std::array<char, 8> header{};
        in.read(header.data(), static_cast<std::streamsize>(header.size()));
        VISOR_CHECK_EQ(in.gcount(), static_cast<std::streamsize>(header.size()));
        VISOR_CHECK((std::string_view{header.data(), header.size()} == "P4\n13 5\n"));
```

文件的格式知识写进断言，比写在注释里多一层保险：注释会随实现漂移，断言红了就是真红了。

## 测试的四个回合

第一回合，奇宽整幅交接。13×5 的画布画三笔（整行、单点、两见方的小块），交给同尺寸的宿主，逐像素一致、收一次、零拒收：

```cpp
    OddCanvas source;
    const Pen brush{source, Color::On};
    brush.fill(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 13, .h = 1}});
    brush.plot(PointI{6, 3});
    brush.fill(RectI{.pos = PointI{9, 2}, .size = SizeI{.w = 2, .h = 2}});

    OffscreenHost mirror(13, 5);
    VISOR_CHECK(visor::host::submit_frame(mirror.table(), source));
    VISOR_CHECK_EQ(mirror.flush_count(), 1);
    VISOR_CHECK_EQ(mirror.rejected_count(), 0);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 13; ++x) {
            VISOR_CHECK_EQ(mirror.get((PointI{x, y})), source.get((PointI{x, y})));
        }
    }
```

第二回合，第二次提交照常计数，空表被拒且任何计数都不动，这是“不崩”的最小验证：

```cpp
    // A second submission counts; an empty table is refused without a crash
    // and without touching any counter.
    VISOR_CHECK(visor::host::submit_frame(mirror.table(), source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);
    const HostTable empty_table{};
    VISOR_CHECK(!visor::host::submit_frame(empty_table, source));
    VISOR_CHECK_EQ(mirror.flush_count(), 2);
```

第三回合，尺寸不符。12×5 的宿主收 13×5 的整幅，记一次拒收、自己的像素保持全灭：

```cpp
    OffscreenHost smaller(12, 5);
    VISOR_CHECK(visor::host::submit_frame(smaller.table(), source));
    VISOR_CHECK_EQ(smaller.rejected_count(), 1);
    VISOR_CHECK_EQ(smaller.flush_count(), 0);
    VISOR_CHECK_EQ(smaller.get((PointI{0, 0})), Color::Off);
```

第四回合，PBM 往返。写文件、读回来、头部 8 字节对字面值、10 个行字节与画布逐个相等、文件到此为止（`peek` 应读到 EOF），最后删掉文件：

```cpp
        std::array<char, 10> rows{};
        in.read(rows.data(), static_cast<std::streamsize>(rows.size()));
        VISOR_CHECK_EQ(in.gcount(), static_cast<std::streamsize>(rows.size()));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            VISOR_CHECK_EQ(static_cast<std::uint8_t>(rows[i]), source.pixels()[i]);
        }
        VISOR_CHECK(in.peek() == std::ifstream::traits_type::eof());
    }
    VISOR_CHECK(std::filesystem::remove(path));
```

## 验收

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`host` 全部用例通过，PBM 往返也在里面。

再亲手看一眼那个文件。编辑 `tests/host_tests.cpp`，把最后删文件的一行临时注释掉：

```cpp
    // VISOR_CHECK(std::filesystem::remove(path));
```

重新构建、跑测试。测试的工作目录是 `build/tests/`，文件落在那里。用十六进制查看工具看它：

```bash
cmake --build build
ctest --test-dir build
xxd build/tests/visor_test.pbm
```

预期能对上这张表：

```
00000000: 5034 0a31 3320 350a fff8 0000 0060 0260
00000010: 0000
```

前 8 个字节 `50 34 0a 31 33 20 35 0a` 就是 `P4\n13 5\n`，你数得出的那 8 个。后面 10 个字节是五行：`ff f8` 是画满的第一行（第四章的 0xF8 回来了），`00 00` 是空的第二行，`00 60` 与 `02 60` 是那个两见方小块与单点的落点，最后一行 `00 00`。这张图随测随删（测试读回验证后立即删除），项目里第一张留存下来的图属于第七章。看明白之后把删文件那行恢复，重新跑一遍确认通过。

下一章是 B1 的关卡：五矩形场景、三轮比对、消毒器（即第一章的 ASan/UBSan）两种构建各跑一遍，还有项目第一张留存下来的图片。

# 第三十八章 像素格式：三份 traits 与两条收帧的路

第三十一章结尾我们留了一句话：「到那时我们让 traits 接手 `mono_ink`，RGB565 与 XRGB8888 各自实现一份。」这一章讲的就是兑现那一批。它落完之后，画布有了第二个模板参数，三种像素格式各有一份 traits，宿主认得每一种。最重要的一句放在前面：RGB565 与 XRGB8888 进来，控件一行都没动，第三十一章立好的角色、墨水、像素三层原样接住了它们。

## mono_ink 搬家

我们回头翻第三十一章，Mono1 怎么把一个 `Ink` 落成像素，规则写在 `mono_ink` 那个函数里：取 RGB 三个字节，只要不是全黑就算亮。这一批我们把规则搬进了 `Mono1Traits::from_ink`，`mono_ink` 删掉，全仓再没有第二个翻译点。

跟着一起搬家的还有 `pixel_bit_position`。它原来住在画布的头文件里，我们翻开 `include/visor/canvas/pixel_traits.hpp` 一看，公式没变：

```cpp
constexpr std::uint8_t pixel_bit_position(std::size_t x) noexcept {
    return static_cast<std::uint8_t>(7 - (x % 8));
}
```

第 0 号像素骑在字节的最高位上，MSB 在前。我们全仓搜它的名字，消费方一眼数得齐：画布打包、离屏宿主、X11 宿主、SSD1306 的页缓冲、Cinux 的帧展开、字模的读位，全都到这个头文件里来问。公式没有复制出第二份，只是住处换到了这里。

## 三份 traits

我们打开这个头文件，每种格式是一个结构体，六样词汇：

- `Pixel`，这个格式的像素类型
- `bytes_per_row(width)`，一行多少字节
- `from_ink(ink)`，`Ink` 怎么落成本格式的像素
- `store` 与 `load`，往一行里写一个像素、从一行里读一个像素
- `fill`，把整块表面铺成一个像素

全部 constexpr，住在头文件里，不带任何存储。画布和画笔参数化在这个结构体上，不参数化在格式名上，我们往后加一种格式，写的是一个结构体，别的什么都不用加。

我们从 Mono1 看起。它的 `Pixel` 是 `bool`，`bytes_per_row` 是 `(width+7)/8`，尾字节算在内。`from_ink` 就是原 `mono_ink` 的规则：

```cpp
[[nodiscard]] static constexpr Pixel from_ink(Ink ink) noexcept {
    return (ink.argb & 0x00ffffffU) != 0;
}
```

打包原样没动，你拿旧判例的期望值直接对，一张单色画布的内存形状一个字节都不变。`fill` 铺完之后，尾字节里越过最后一个像素的填充位保持零。

RGB565 的 `Pixel` 是 `std::uint16_t`，红五绿六蓝五。绿多一位不是随手分的，眼睛对绿更敏感。字节序小端，低字节在前，小彩屏面板和桌面的 16 位字都认这个顺序。你拆通道，`from_ink` 各留高位：

```cpp
const std::uint32_t red = (ink.argb >> 16) & 0xffU;
const std::uint32_t green = (ink.argb >> 8) & 0xffU;
const std::uint32_t blue = ink.argb & 0xffU;
return static_cast<Pixel>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
```

行里没有填充位，你数下来 stride 就是宽度的两倍。

最后一份是 XRGB8888，我们把它摊开：`Pixel` 是 `std::uint32_t`，就是 `Ink` 那个 `0xAARRGGBB` 字，alpha 字节读作「忽略」。`from_ink` 只有一行：

```cpp
return ink.argb | 0xff000000U;
```

主题值在这里改不动透明度，你给 `Ink` 带什么 alpha，像素进来都是不离身的 0xff。行是宽度的四倍，同样小端。

## 画布的第二个模板参数

画布这边我们也动了一笔。`include/visor/canvas/canvas.hpp`（原来叫 `mono_canvas.hpp`，这一批改了名）里，模板签名现在是：

```cpp
template <base::SizeI CanvasSize, typename Format = Mono1Traits>
class Canvas {
```

第二个参数默认 Mono1，我们靠它省掉了成片的改动：此前写下的所有 `Canvas<Size>` 原样编过，照旧是单色画布，只有想要彩色的调用点把格式写出来：

```cpp
Canvas<kFrame> mono;                    // 还是单色
Canvas<kFrame, Rgb565Traits> panel;     // 16 位彩色
Canvas<kFrame, Xrgb8888Traits> wide;    // 32 位
```

`kStrideBytes` 从 traits 算。我们拿同样八像素宽试三种画布，行字节数分别是 1、16、32。

画布新收一个 `fill(Pixel)`，把整幅铺成本格式的某个像素。`set_clear(bool)` 留着，语义落成「亮是本格式的白，暗是本格式的黑」：单色画布下逐位等于旧行为，彩色画布下铺出来的是白底或黑底。你在 RGB565 画布上 `set_clear(true)`，随便抽个像素读回来，是 0xffff。同一个动作换到 XRGB8888 画布上，读回 0xffffffff。两种画布拿到的都是真正的白。

越界口径没变。你越界试一下也一样：`set` 越界返回 false，一个像素不动，`get` 越界读本格式的零，单色是不亮，彩色是黑。画笔那一层的立场照旧是笔画不查边界，画布是你能指望的最后一道检查。

接缝那一层只换了一张表的类型。`CanvasPainter` 构造时把七个角色一次翻成像素，表从七个 `bool` 换成七个 `CanvasT::Pixel`，翻译规则换成 `CanvasT::Traits::from_ink`。四个动词的签名保持原样，控件报的还是角色名。我们在彩色画布上画整棵树，控件不用跟着改，第三十一章的控件判例原样跑。

## 像素转换：65536 个往返

两个格式之间的换算，我们放进新头文件 `include/visor/canvas/pixel_convert.hpp`。`rgb565_to_xrgb8888` 用按位复制：通道从五位或六位补到八位，把高位的值再抄一遍进低位。

```cpp
const std::uint32_t red8 = (red << 3) | (red >> 2);
const std::uint32_t green8 = (green << 2) | (green >> 4);
const std::uint32_t blue8 = (blue << 3) | (blue >> 2);
```

为什么要抄那几位？我们只左移三位试一次：565 的白落出来红通道是 0xf8，不是 0xff，最亮的一档到不了头。抄了高位之后，白落在 0xff。反方向 `xrgb8888_to_rgb565` 各通道留高位，alpha 忽略，与 traits 的 `from_ink` 是同一次截位。

宽方向精确，窄方向有损，所以宽出去再窄回来，对全部 65536 个窄像素恰好是恒等往返。判例里那个循环替我们一个值一个值地过：

```cpp
for (std::uint32_t value = 0; value <= 0xffffU; ++value) {
    const auto narrow = static_cast<std::uint16_t>(value);
    VISOR_CHECK_EQ(xrgb8888_to_rgb565(rgb565_to_xrgb8888(narrow)), narrow);
}
```

我们什么时候用得上它们？头注释写的场景是：客户端说的是 8888，面板或者合成器自己的表面说 565，中间得有人把宽像素收窄。两个函数都是 constexpr 的纯函数，不带状态，谁想用谁就 include。

## 线上格式与一张对照表

我们把视角挪到宿主一侧，那里有自己的枚举，在 `include/visor/bindings/display.hpp`：

```cpp
enum class PixelFormat : std::uint8_t { Mono1, RGB565, XRGB8888 };
```

`FlushRegion` 带一个 `format` 字段，宿主向核心报屏幕尺寸与格式的 `HostInfo` 也带一个，`row_bytes(format, width)` 按枚举答行字节数。你翻遍画布的头文件也找不到这个枚举的名字：画布只知道自己参数化在哪个 traits 上，过界的字节布局说什么语言是宿主的词。

两个词表之间的对照，我们写成一张显式的表：`WireFormat` 对三份 traits 各有一份特化，`kWireFormat<Traits>` 是取值入口：

```cpp
template <>
struct WireFormat<canvas::Rgb565Traits> {
    static constexpr PixelFormat kValue = PixelFormat::RGB565;
};
```

我们把对照放在宿主一侧，让画布报自己的像素类型、宿主说过界的字节布局，表里一种格式一行，两个词表的关系全在这里。新格式忘了说自己线上说什么，第一次 flush 就编不过，不会拖到运行时才出错。

真用到表的只有两家，我们不用在别处找：`submit_frame` 与 `render_window`，它们自动带上画布自己的格式，不让人写第二遍：

```cpp
return submit_region(full, canvas.pixels().data(), CanvasT::kStrideBytes,
                     kWireFormat<typename CanvasT::Traits>);
```

你要手写调用，口子留在 `submit_region`，它显式收一个格式参数，默认 Mono1。同一个格式只在一处命名，彩色画布交到彩色宿主，中间没有人手抄一遍枚举。

第三十一章留话里说 Mono1 的默认值会删掉。真到动手这一批，我们把它留下了，理由就写在参数旁边：这一章之前的调用点说的都是 Mono1，默认参数让他们照旧编过。新代码不必写它，`submit_frame` 会自己带上。

## 彩色主题与单色档的取舍

主题那边我们备了一份彩色预设 `kColorTheme`，在 `include/visor/ui/theme.hpp` 里，七个角色都有各自的墨水：

```cpp
inline constexpr Theme kColorTheme{
    .background = canvas::Ink::black(),
    .foreground = canvas::Ink::from_rgb(0x00e7ebf5),
    .muted = canvas::Ink::from_rgb(0x008a93a6),
    .accent = canvas::Ink::from_rgb(0x004a62c8),
    .on_accent = canvas::Ink::from_rgb(0x00f2f5ff),
    .border = canvas::Ink::from_rgb(0x0039435c),
    .disabled = canvas::Ink::from_rgb(0x00555d70),
};
```

我们照着表念一遍：底是纯黑，前景近白，强调是鸢尾蓝 0x4a62c8，面上的字配 0xf2f5ff。剩下的 `Muted`、`Border`、`Disabled` 是同一支蓝灰的三步，让禁用态读得出是禁用，又不用改控件的形状。色值归作者调，控件全在报角色，整套界面换色只改一张表。

底色用纯黑是有意的。Mono1 只把一个值读成不亮，那个值就是黑。我们把同一张主题分别画到单色与彩色两张画布上，单色那幅底还是暗的、其余角色全是亮，整幅不翻面，代价是失掉灰阶：`Muted`、`Border`、`Disabled` 全落成亮，蓝灰三档拉不开。单色格式只给一个自由度，灰阶本来就不是它的词。取舍写进了判例：单色屏用单色预设，彩色屏才谈灰阶。

## 宿主收彩色帧的两条路

离屏宿主 `OffscreenHost` 的构造函数多了格式这一档。你不传，`OffscreenHost(w, h)` 还是那个单色替身，传了 `OffscreenHost(w, h, format)`，它就说另一种格式。它只收自己格式的区域，别的格式拒收并计数，口径与越界相同。

读回来的路也按格式分。`get_pixel` 按宿主自己的格式读，放宽到 32 位：Mono1 答零或一，RGB565 答 16 位字，XRGB8888 答 `0xAARRGGBB`。你读彩色帧就换 `get_pixel`，单色那个 `get` 在彩色宿主上答 false。PBM 只给 Mono1：彩色帧没有 PBM 的写法，`save_pbm` 答一个 false，不写一张误导人的图。

另一头是 X11 宿主，我们看它的 `open`：多收一个格式参数，默认还是 Mono1，单色的路照旧。彩色区域在最后一步铺到窗口视觉的通道掩码上，掩码从窗口的 visual 读回来。`place_channel` 用 `countr_zero` 找掩码的位移，用 `popcount` 量掩码的宽度，把一个 8 位通道放进这个视觉自己的位置：

```cpp
const auto shift = static_cast<unsigned>(std::countr_zero(mask));
const auto width = static_cast<unsigned>(std::popcount(mask));
const std::uint32_t taken = width >= 8 ? channel : (channel >> (8 - width));
return (taken << shift) & mask;
```

一台把通道排得更宽或者换个顺序的服务器，读到的掩码不同，铺出来的字照样对，我们不预设某台机器的排布。窗口跟着屏幕的默认深度走，桌面通常是 24 位，缩放、影子帧、暴露重放这些单色路建立起来的机制，彩色照用。

我们怎么知道铺对了？`tests/x11_host_tests.cpp` 的彩色回读段自己起一台 Xvfb，把白色整帧读回来，读到的是红绿蓝三个掩码按位或，纯红整帧读回来只有红掩码一个通道亮。单色帧交给彩色窗口，照拒收并计数。

## --color：开窗看颜色

示例 `examples/x11_window` 多收一个 `--color` 参数。同一棵控件树、同一批控件，我们换成 RGB565 画布、说 RGB565 的窗口和彩色预设，只动一个启动参数：

```bash
./build/examples/x11_window --color
```

不加参数还是单色窗口。两条路共用示例里跑窗口循环的模板函数 `run_window`，格式只决定一个像素是什么，所以两种窗口画出来的是同一套控件。WSLg 里 DISPLAY 已经指好，你直接跑就能开窗，Tab 在控件间走，方向键调滑块，Enter 激活，关窗口退出。

你手上没有 X server，也有一条看图的路。`--snapshot-color` 把同一棵树画进一张 RGB565 画布，逐像素过一遍 `rgb565_to_xrgb8888`，写一张 PPM 出来。PBM 一位一像素，装不下彩色，PPM 是 PBM 的彩色版，每像素记红绿蓝三个字节，看图软件都能开：

```bash
./build/examples/x11_window --snapshot-color color.ppm
```

## 判例

`tests/pixel_tests.cpp` 注册了十二段判例，按注册名列出，你打开文件对着读：

- `the_default_format_is_mono`：不写格式仍是单色画布，`Pixel` 是 `bool`，行字节数 1。
- `rgb565_pixels`：行字节数 16，`from_ink` 的三个落值（黑 0x0000、白 0xffff、鸢尾蓝 0x4a62c8 落 0x4b19），字节序（x=1 从字节 2 起，低字节 0x19 在前），邻居不动，越界拒绝与零读，`set_clear` 两档，整幅 `fill` 逐像素过。
- `xrgb8888_pixels`：alpha 进来就强制 0xff（0x00112233 与 0x80112233 都落 0xff112233），鸢尾蓝落 0xff4a62c8，字节序，邻点不动。
- `format_conversions`：两个端点值（0x0000 落 0xff000000、0xffff 落 0xffffffff），鸢尾蓝往返，65536 个值逐个恒等往返，宽方向只差各通道低位的两个蓝（0xff112233 与 0xff172033）落同一个 565 像素。
- `painter_in_color`：七格角色表翻成像素，五档有落面断言，底是黑，强调面是鸢尾蓝，`Muted`、`Border`、`Disabled` 三个像素两两不同。
- `strokes_in_color`：`plot` 单点着墨邻居不动，圆角填充的行中着墨、角盘外着墨、角盘内留暗。
- `theme_switch_in_color`：同一个矩形换预设，强调面的像素从 0xffff 换成 0x4b19，控件一行没改。
- `color_theme_on_mono`：彩色主题落到单色画布，底不亮、`Muted` 亮，把取舍固定成断言。
- `bitmap_tinted_by_role`：字模的字形按角色着色喷上画布，亮的源像素落鸢尾蓝，暗的不画，scale 2 铺成 2x2 块。
- `host_receives_color`：RGB565 帧进 RGB565 宿主，读回 0x4b19，越界读零，同一帧进单色宿主拒收一次、成功零次，彩色宿主上单色读答 false，`save_pbm` 答 false。
- `render_window_submits_the_canvas_format`：`render_window` 把画布格式带到宿主，强调面整帧落鸢尾蓝，格式没写第二遍。
- `color_preset_roles`：预设自己的几档关系，底是黑、强调是鸢尾蓝、前景与底不同、`on_accent` 与 `accent` 不同、`muted` 与 `disabled` 不同。

你在仓库根目录跑一遍：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功的条件是汇总那行以 `100% tests passed` 开头，退出码 0。这十二段都编在 `visor.tests` 里，红一条这一行就不过。我这次跑出来是 `100% tests passed out of 4`，四个目标全过。

红绿实验两个，你可以自己做。第一个，把 `rgb565_to_xrgb8888` 里 `red8` 的补位改成只左移（`red << 3`），重新构建跑测试：端点断言 `rgb565_to_xrgb8888(0xffff) == 0xffffffff` 会红，红通道落 0xf8。65536 往返那段循环这时仍然绿。我们顺着位看就明白：宽方向抄进低位的那几位，窄方向只取高位，抄没抄都不影响结果。两个断言一起在，宽方向的值和往返两边才都有人看住。改回去，恢复绿。

第二个，你把 `Mono1Traits::from_ink` 的比较改成 `== 0xffffffU`，只有纯白才算亮。`color_theme_on_mono` 会红：`Muted` 的 0x8a93a6 落成不亮，判例期望亮。单色的控件判例照旧绿，单色预设的墨水本来就是纯白纯黑。改回去，恢复绿。

## 这一批没做的事

没做的事我们照实记下来。条带还没进 traits：画布现在只有格式一维，缓冲的形状（整幅、水平条带、往后的交换缓冲）是后面的批次，渲染核心仍然不假设缓冲等于屏幕，第十一章立的边界没变。

alpha 仍然只是不透明标记，混合是后面一层。XRGB8888 的宿主路已经通了，你跑示例还只见 Mono1 与 RGB565 两档，桌面示例换 32 位画布是往后的事。

## 验收

整串验收我们一条命令跑完：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍代码，fontgen 的独立构建与测试收尾。CTest 现在挂着四个目标：`visor.tests`、`visor.signal.selftest`、`visor.snake.selftest`、`visor.popup.selftest`。你盯末行，打印 `acceptance: all green` 才算过，中间任何一道停下都不算。

收尾你亲手玩一次主题切换。`./build/examples/x11_window --color` 打开彩色窗口，Tab 走到外观那组单选，选 NIGHT，彩色退成纯白墨，选 PAPER，整幅翻成白底黑字，RGB565 里的白是实实在在的 0xffff。再不带参数跑同一个示例，同一批控件画出来的还是黑白两值，这一批的改动清单里没有控件实现文件。

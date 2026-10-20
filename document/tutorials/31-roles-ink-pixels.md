# 第三十一章 角色、墨水与像素

第二十五章结束时，控件手里只有两个语义色：`background` 和 `foreground`。旧的 `Color::On` 是墨、`Color::Off` 是底，这一章把它们分别交给 `Foreground` 与 `Background`。`canvas::Color` 这个类型也属于拆分前的形状。单色屏上我们只用两个语义色就够，桌面要彩色。到格式批，我们铺开两档：RGB565 是每像素 16 位的彩色格式。XRGB8888 是每像素 32 位，最高字节留空。光靠两个语义色撑不到那里。

麻烦出在接口的形状上。`Painter` 是运行时虚接口，控件是运行时对象，画布却按尺寸在编译期定死。要是每种像素格式各带一个颜色类型，`Painter` 的四个动词和所有控件都得跟着模板化，我们加一种屏幕，就多一套控件类型。今天看不出代价，加第二种格式的那天就得付。

我们选的路是把颜色拆成三层。控件只报角色的名字，具体颜色由主题决定。到了画布那一层，颜色已经落成它自己的像素。另一条路是让控件继续传 32 位颜色值，换角色的事推到以后。代价是换角色的那天，每个控件都要再动一遍。本章只做角色与 `Ink`，像素格式留到格式批。

## 角色

角色回答「这一笔是干什么用的」。打开 `include/visor/canvas/color.hpp`，你会看到七个槽位：

```cpp
enum class ColorRole : std::uint8_t {
    Background,
    Foreground,
    Muted,
    Accent,
    OnAccent,
    Border,
    Disabled,
};
```

`Accent` 与 `OnAccent` 成对：强调面用 `Accent`，面上的字用 `OnAccent`，对比由主题保证。我们不需要自己算反色，也不需要在两个值之间猜。`Muted` 管次要文字，滑块轨道也用它。`Border` 画边框。`Disabled` 是给禁用态留的槽位。`Background` 与 `Foreground` 最常用。枚举后面紧跟着 `kColorRoleCount`，由 `ColorRole::Disabled` 加一算出来。

我们也可以反过来给一个按钮分派角色：实心面用 `Accent`，空心面用 `Background`，边框用 `Border`，字用 `Foreground` 或 `OnAccent`。换成深色主题，按钮画出的像素整体翻面，判例里的极性对照照样成立。

## 墨水

墨水回答「主题挑了什么颜色」。`Ink` 是包着 32 位值的小结构体，值按 `0xAARRGGBB` 解释，我们按这个字面就能拆出 alpha 和红绿蓝三段。下面只摘字段和几个静态函数，比较运算符略去：

```cpp
struct Ink {
    std::uint32_t argb = 0;
    // ...

    static constexpr Ink black() noexcept { return Ink{0xff000000}; }
    static constexpr Ink white() noexcept { return Ink{0xffffffff}; }

    static constexpr Ink from_rgb(std::uint32_t rgb) noexcept {
        return Ink{0xff000000U | (rgb & 0x00ffffffU)};
    }
};
```

alpha 目前只当作不透明标记，混合留到以后。Mono1（每像素一位的单色格式）怎么落这个值，规则写在 `mono_ink` 里：取 RGB 三个字节，只要不是全黑就算亮。我们在主题里写的仍然是 `Ink`，眼下由 `mono_ink` 决定落成什么像素。每种格式将来各写一个 traits 结构，交代自己的像素类型、每行字节数，以及 `Ink` 怎么落成像素。

## 像素

像素是每种格式自己的存储单位。Mono1 的像素是一位，在 C++ 里是 `bool`，RGB565 的像素是 16 位，XRGB8888 是 32 位。我们把画布的像素接口改成只收自己的像素值。它不收角色，也不收墨水。`set` 收一个 `bool`，`get` 返回一个 `bool`。`set_clear` 也收 `bool`，把整幅点亮或清掉。签名就在 `include/visor/canvas/canvas.hpp` 里，一共三处签名。

`Pen` 的第二个模板参数从「墨水类型」改成「像素类型」。Mono1 的笔就是 `Pen<CanvasT, bool>`，笔只认像素，不认角色。我们在控件里写的角色，到这一层已经翻成像素值，再往下的画布只看得见像素值。

## 接缝：一张七格表

`CanvasPainter` 是角色与像素之间的接缝。换一种像素格式，你会问，四个动词的签名要不要跟着各改一遍？不用。它在构造时把七个角色一次翻成七个像素值，存成一张小表：

```cpp
void resolve(const Theme& theme) noexcept {
    for (std::size_t i = 0; i < canvas::kColorRoleCount; ++i) {
        const auto role = static_cast<canvas::ColorRole>(i);
        const canvas::Ink ink = theme.ink(role);
        pixels_[i] = canvas::mono_ink(ink);
    }
}

[[nodiscard]] bool pixel(canvas::ColorRole role) const noexcept {
    return pixels_[static_cast<std::size_t>(role)];
}
```

表是 `std::array<bool, canvas::kColorRoleCount>`，随 painter 活一帧。`fill`、`plot`、`draw_bitmap` 每次调用查一次表。`fill_rounded` 逐行调 `fill`，每个跨度查一次。我们换格式时，换的是表的类型和翻译规则，四个动词的签名留在原处。

## 主题

你对照第二十五章的版本看，`Theme` 的两个颜色字段扩成七份墨水，圆角半径照旧。下面只摘字段，`ink()` 在后面：

```cpp
struct Theme {
    canvas::Ink background = canvas::Ink::black();
    canvas::Ink foreground = canvas::Ink::white();
    canvas::Ink muted = canvas::Ink::white();
    canvas::Ink accent = canvas::Ink::white();
    canvas::Ink on_accent = canvas::Ink::black();
    canvas::Ink border = canvas::Ink::white();
    canvas::Ink disabled = canvas::Ink::white();
    CornerRadius corner_radius{};
    // ...
};
```

`Theme::ink(ColorRole)` 用一个穷举 switch 把角色翻成墨水。以后我们加角色忘了给主题补一档，编译器会警告。switch 末尾留了一句 `return foreground` 兜底，只在警告被无视时走到。

Mono1 只有亮暗两档。我们把 `kLightTheme` 的七个角色填成两组墨水：

| `kLightTheme` 的墨水 | 角色 |
|---|---|
| `Ink::white()` | `foreground`、`muted` 与 `accent`，另有 `border`、`disabled` |
| `Ink::black()` | `background`、`on_accent` |

我们把七个角色过一遍 `mono_ink`，只落下亮与暗两种像素。单色判例里每一笔都对得上原来的像素值。

预设名有一处容易读反，你读的时候留意：浅色和深色是极性名，不是明暗名。旧代码的 light 是暗底亮墨，`kLightTheme` 沿用这个极性，单色判例不用整体反过来。

主题跟一帧走。每个 painter 构造时从根控件取一次，交给 `CanvasPainter`。`Widget::set_theme` 仍然沿树报脏，但在子树上调用它只申报重画，不改变这一帧的调色板，因为表在构造 painter 那一刻就算好了。我们构造 painter 时只写画布不传主题，画出来就是 `kLightTheme` 的极性，树上的深色主题白换了。渲染循环里把从根取到的主题传进去。

## 控件怎么用

控件不再收具体颜色，只写角色。下面把各控件现在用的角色列成表，你读实现时对着看。

| 位置 | 角色 |
|---|---|
| 按钮实心面 | `Accent`，面上的字 `OnAccent` |
| 按钮空心面 | `Border` 边，`Background` 底，`Foreground` 字 |
| 复选框外框 | `Border`，选中的芯 `Accent`，勾 `OnAccent` |
| 单选圈 | `Border`，选中的点 `Accent` |
| 滑块轨道 | `Muted`，手柄 `Accent`，手柄内孔 `Background` |
| 文本框 | 边框 `Border`，文字与光标 `Foreground`（可换成 `Muted` 或 `Disabled`） |
| 下拉高亮行 | `Accent` 底，`OnAccent` 字 |
| 旋钮 | 环与刻度 `Foreground`，指针与轴 `Accent` |
| 标签 | `Foreground`，也可以传 `Muted` |
| 矩形装饰 | 默认 `Foreground`，面板传 `Background` |
| 焦点环 | `Foreground` 与 `Background` 交替 |

实心与空心两条路怎么共用一段绘制？我们读 `Button::on_draw` 时能看到答案：它算完 `solid` 再按真假挑三档角色。

```cpp
const CornerRadius corners = theme() != nullptr ? theme()->corner_radius : CornerRadius{};
const bool solid = outlined_ == pressed();
const auto edge = solid ? canvas::ColorRole::Accent : canvas::ColorRole::Border;
const auto face = solid ? canvas::ColorRole::Accent : canvas::ColorRole::Background;
const auto text = solid ? canvas::ColorRole::OnAccent : canvas::ColorRole::Foreground;
painter.fill_rounded(bounds(), edge, corners);
if (!solid) {
    painter.fill_rounded(base::inset(bounds(), 1), face, inset_corners(corners));
}
```

`solid` 是 `outlined_ == pressed()`，你按这个等式去读两种按钮。默认按钮松开时 `solid` 为真，整块 `Accent`、字 `OnAccent`。按下时 `solid` 变假，退成 `Border` 一圈、`Background` 底、`Foreground` 字。`set_outlined(true)` 的按钮把这个关系反过来：松开是空心，按下才实心。按钮只决定用哪一档角色，颜色由主题给。深色主题现在就是换一张表，同一段代码画出的像素整体翻面，判例里亮暗两套逐像素互补。彩色主题等格式批。

## 判例实录

接口改完，我们最容易漏的是主题的传递。语言规则和批量替换也各留下一条，判例的写法跟着换。

有一处漏在主题的传递上。`CanvasPainter{frame}` 的默认主题是 `kLightTheme`，我们画一棵已经换了深色主题的树，构造 painter 只写画布，出来还是亮底。信号台（`examples/signal`）与贪吃蛇（`examples/snake`）的快照路径、主题判例都补上了这个参数。主题在构造 painter 时传一次，渲染层从根取就够。

C++ 还有一条规则让我们绕了一下。`Ink` 最初写成 `static constexpr Ink Black{...}`，类内初始化时类型还没完整，GCC 直接拒绝。改成 `black()` 与 `white()` 两个静态函数，读起来也更像在取一个值。

批量替换 `Color::On` 的时候留下过 `visor::canvas::true` 一类的半截限定名，编译一轮才清干净。机械替换之后我们让编译器过一遍，比用眼睛找可靠。

`tests/widget_tests.cpp` 链进 `visor.tests`，我们跟着改了判例的写法。`Painter` 替身只记 `fill`、`plot`、`fill_rounded` 收到的 `ColorRole`，`draw_bitmap` 那一路只记位置与缩放。角色断言在替身里做，像素断言在画布上比 `true` 与 `false`。

## 验收

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

你跑一遍，六道全部通过。我这次跑出来是 rel 133.64 秒加信号台 0.01 秒、贪吃蛇 0.00 秒，ASan/UBSan 135.09 秒加 0.04 与 0.03 秒，TSan 135.01 秒加 0.05 与 0.04 秒，clang-format、clang-tidy 全过，fontgen 两个用例也全过。换一台机器，这些秒数会变。验收输出最后一行是 `acceptance: all green`。

信号台自测还是 9943 个点，贪吃蛇还是 1456 个点。你可以在自测输出里核对，两个数与拆分之前一致，单色像素一个也没动。

你也可以自己改两处试试。头一处改 `Theme::ink` 里 `Foreground` 那一档，让它返回 `background`，再跑 `visor.tests`，文本框里的字和焦点环会一起暗掉，主题判例会红，复原再绿。另一处把 `CanvasPainter::resolve` 里的 `pixels_[i]` 全改成 `true`，按钮、复选框、下拉框会一起失去明暗，像素判例红，复原再绿。

## 下一批的口子

这一章只碰 Mono1 这一档像素格式。我们下一批扩事件的词表，补上滚轮与修饰键，再让分发带上时间参数。像素格式排在后面的格式批。

到那时我们让 traits 接手 `mono_ink`，RGB565 与 XRGB8888 各自实现一份。`submit_region` 改成显式收格式，`FlushRegion` 补上字节序，Mono1 的默认值删掉。格式批只动像素那层和宿主，现有的控件判例照常跑。

`Disabled` 角色已经进表，我们还没接上控件的禁用态。菜单与弹层排在后面的批次，词表扩充是下一批。

前三十章里出现 `Color::On`、`Color::Off` 和 `canvas::Color` 的片段都是拆分前的形状，你读到那里，按本章的词汇理解它们。

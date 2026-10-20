# 第三十二章 滚轮、修饰键与时间

第三十一章收尾时，事件表只有五个词：`KeyDown`、`KeyUp`、`PointerMove`、`PointerDown`、`PointerUp`。信号台的旋钮能转，贪吃蛇的蛇能转向，靠的都是五个词。事件表里没有滚轮的位置，也没有修饰键的位置，一轮分发里还没有一个共享的时间。这一章我们补上滚轮、修饰键与时间，控件这一层暂时不接滚轮。

你打开 `include/visor/bindings/events.hpp`，看事件表变成了什么样。

## 事件表添第六个词

`Event` 是 `std::variant`，它的每个候选类型就是一个事件词。你看 `PointerWheel`，它是第六个事件词，带四个字段：

```cpp
struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

struct KeyDown {
    std::uint32_t code;
    Modifiers modifiers{};
};

struct PointerWheel {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    base::PointI pos;
    Modifiers modifiers{};
};

using Event = std::variant<KeyDown, KeyUp, PointerMove, PointerDown, PointerUp, PointerWheel>;
```

你把滚轮拨过一格，`dx` 与 `dy` 就加一或减一。两个数的单位是格。有人会按像素或行去读，源码注释把单位定成格，一格就是一个单位。一次收到 2，是同一个词里两格，不是两个词。符号按内容移动的方向定：`dy` 为正表示内容往下走，也就是滚轮朝用户这边转。`dx` 为正表示往右。

我们把 `pos` 定成滚轮发生的位置，用画布坐标，和 `PointerDown` 的 `pos` 同一套。`modifiers` 是滚轮那一刻按住的修饰键。滚轮没有按下与抬起的配对，来一个算一个，所以它不像按键那样有一个释放的词可以关门。

事件词的个数从五个变成六个。你翻到文件末尾，会看到那句 `static_assert(std::is_trivially_copyable_v<Event>)` 仍在。队列只存平凡可拷贝的值，要求照旧。

## 四个具名 bool

我们把 `Modifiers` 写成四个具名 bool。位掩码要你解开一个数，才知道哪一位是 Shift，这里不这么做。代价是结构体比一个字节宽，`Event` 也跟着宽一点。事件队列的容量按个数算，多出来的宽度我们付得起。

你按事件表逐个看，带修饰键的是 `KeyDown`、`KeyUp`、`PointerDown` 与 `PointerWheel`。`PointerMove` 与 `PointerUp` 不带。

我们让 `PointerUp` 不带，是因为按下的那一刻已经拿到了。释放只负责关掉那个按下，控件要判断修饰键，读按下时带来的信息就够，不必在两份里挑一份信。`PointerMove` 不带，是因为移动是位置的采样，不算动作的起点。拖拽与悬停都只要坐标，往每条移动里塞一份与它无关的键盘状态，只是让样本变胖。

我们把 `KeyUp` 留在带修饰键的一边，与「按下已经拿到」并不冲突。抬起时的键盘状态可能已经和按下时不同，抬起这个词自己带着信息，它没有把按下的内容复述一遍。

## 控件回调跟着改

你打开控件基类 `Widget` 的声明，它在 `include/visor/ui/widgets/widget.hpp`。这一排回调一起换了形状：三个老回调多了修饰键参数，旁边多出一个滚轮的虚函数。下面摘声明，函数体都是空的：

```cpp
    virtual void on_pointer_down([[maybe_unused]] base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {}
    virtual void on_pointer_wheel([[maybe_unused]] const host::PointerWheel& wheel) noexcept {}
    virtual void on_key_down([[maybe_unused]] std::uint32_t code,
                             [[maybe_unused]] host::Modifiers modifiers) noexcept {}
    virtual void on_key_up([[maybe_unused]] std::uint32_t code,
                           [[maybe_unused]] host::Modifiers modifiers) noexcept {}
```

`on_pointer_wheel` 是新增的虚函数，默认什么都不做。你写一个不认识滚轮的控件，不必写这一行，基类替它把词接住再丢掉。

你挨个看过去，改过回调签名的控件有七个。Button、Checkbox、Dropdown、Knob、RadioGroup、Slider、TextBox 各改一份，示例 `examples/snake` 里的 `SnakePanel` 也一起改。Label 与 Box 没有重写这些回调，签名变了也碰不到它们。

你翻一遍控件实现，`on_pointer_wheel` 目前还没有一个重写，只有判例里的测试替身实现它。多数控件也不看 `modifiers`。按钮的激活仍然在抬起的词里完成，复选框仍然在按下就翻转，下拉框仍然用 Enter 与 Space。这一章只把参数送到手边，不改任何激活逻辑。参数到了而没人读，是这一章的正常状态。

## 一轮只读一次钟

我们给分发函数的签名添了一个 `now_ms`：

```cpp
void dispatch_event(Widget& root, InputRouter& input, const host::Event& event,
                    std::uint64_t now_ms) noexcept;
```

你打开 `dispatch_event` 的定义，会看到 `now_ms` 标了 `[[maybe_unused]]`，滚轮与按键都不读它。它现在的用途只有一个：让这一轮的事件共享同一个时间原点。

我们让 `run_dirty` 在事件循环之前读一次钟：

```cpp
    int consumed = 0;
    const std::uint64_t now_ms = host::current_time_ms(host);
    host::Event event;
    while (host::try_poll_event(host, event)) {
        ++consumed;
        dispatch_event(root, input, event, now_ms);
        if (std::holds_alternative<host::KeyDown>(event) ||
            std::holds_alternative<host::KeyUp>(event)) {
            break; // one key event per round (chapter 18)
        }
    }
```

我们把 `run_once` 写成同一段，读完钟再排空队列。同一轮进来的十个事件拿到的都是这个数。要是每个事件各读一次钟，一轮里跨过毫秒边界的事件就会落在两个时间上，以后按下与抬起之间的间隔算出来会忽长忽短。时间原点一轮一个，这是我们在这里定下的口径。

你查宿主没接时钟的情况，`current_time_ms` 返回 0。0 也可能是真实读数，所以单看返回值分不清有没有钟。表上另有一格回答这个问题：

```cpp
[[nodiscard]] bool has_clock(const HostTable& host) noexcept;
```

你读 `has_clock` 的实现，它只查表里的 `clock_ms` 有没有接上。`src` 里还没有调用者，判例里已经用它问过两条路。手势要判断时间可不可信，问一下它，再决定用不用手上的 0。

## 滚轮按悬停走

我们让滚轮不进按压会话。分发里你只看到这一小段：

```cpp
void deliver(Widget& root, [[maybe_unused]] Widget*& press_owner,
             const host::PointerWheel& word) noexcept {
    // A wheel is a hover word, not part of the press session: it goes to the
    // topmost widget under the pointer and never changes who owns the press.
    if (Widget* receiver = root.hit_test(word.pos); receiver != nullptr) {
        receiver->on_pointer_wheel(word);
    }
}
```

`press_owner` 在这里是 `[[maybe_unused]]` 的，滚轮从不改它。你把滑块按住再滚，滚轮仍送给指针下面的控件，滑块继续拥有移动与抬起。滚轮这样分，与拖拽一致：按下定了归属，悬停只回答「现在指针在谁上面」。

你在判例里能看清边界。目标控件占 (10,10) 到 (30,30)。判例起手在 (15,15) 滚一格，目标收到第一个滚轮，计数从 0 变成 1。随后在 (15,15) 按下，归属记在目标上，焦点也落在目标。接着在 (2,2) 滚一格，根控件收到滚轮，目标的计数仍是 1，归属仍是目标。再在 (200,200) 滚一格，那里谁也命中不到，词被丢掉，归属不动。最后在 (2,2) 松手，抬起仍然送给目标，归属才清空。

你要是把滚轮改成跟着归属走，按住之后在 (2,2) 与 (200,200) 滚的两格都会送到目标。判例里两个滚轮计数就是红的地方。

## X11：按钮 4 到 7

我们给 X11 的键翻译添了一个 `state` 参数：

```cpp
std::optional<host::Event> translate_key(std::uint32_t keysym, bool pressed,
                                         unsigned int state) noexcept;
```

`state` 是 X 给的事件修饰掩码。我们把四个位翻成四个具名 bool：

```cpp
host::Modifiers modifiers_from_state(unsigned int state) noexcept {
    return host::Modifiers{
        .shift = (state & ShiftMask) != 0,
        .ctrl = (state & ControlMask) != 0,
        .alt = (state & Mod1Mask) != 0,
        .super = (state & Mod4Mask) != 0,
    };
}
```

你对照 X 的头文件读这四行。ShiftMask 对应 shift，ControlMask 对应 ctrl，Mod1Mask 对应 alt，Mod4Mask 对应 super。Mod1 在多数桌面上是 Alt 键，Mod4 是 Super 键。这个对应是 X 的约定，我们不另造一套。

你翻 X 的按钮定义会看到，X11 没有滚轮轴，滚轮是按钮 4 到 7。Xlib 只给 Button1 到 Button5 起了名字，6 与 7 我们在本地命名：

```cpp
constexpr unsigned int kWheelLeft = 6;
constexpr unsigned int kWheelRight = 7;
```

你按编号看，按钮 4 往上、5 往下、6 往左、7 往右，都在按下时出一个 `PointerWheel`：

```cpp
    if (event.button >= Button4 && event.button <= kWheelRight) {
        if (event.type != ButtonPress) {
            return std::nullopt; // the release of a wheel notch is not a second notch
        }
        std::int32_t dx = 0;
        std::int32_t dy = 0;
        switch (event.button) {
            case Button4:
                dy = -1;
                break;
            case Button5:
                dy = 1;
                break;
            case kWheelLeft:
                dx = -1;
                break;
            case kWheelRight:
                dx = 1;
                break;
            default:
                return std::nullopt;
        }
        return host::Event{
            host::PointerWheel{.dx = dx, .dy = dy, .pos = pos, .modifiers = modifiers}};
    }
```

我们让按钮的释放不再出第二个词，`ButtonRelease` 直接返回空。一个滚轮格只有一个按下，没有配对。按钮 1 到 3 的按下带修饰键，抬起不带，按下位置的坐标原样进入 `PointerDown`。

## Cinux：三个旗，没有滚轮

你打开 Cinux 的线上结构，`WireKey` 里已经有 shift、ctrl、alt 三个 bool，翻译时我们直接搬进 `Modifiers`：

```cpp
            const host::Modifiers modifiers{
                .shift = wire.key.shift,
                .ctrl = wire.key.ctrl,
                .alt = wire.key.alt,
            };
```

你数 `WireKey` 的字段，会发现它没有 super 位。Cinux 路上的 `Modifiers.super` 一直是 false，读者容易怀疑漏填，原因在线上结构里没给 super 留位置。

你数 `WireEvent` 的类型，只有 Move、MouseDown、MouseUp、KeyDown、KeyUp 五种，没有滚轮。滚轮得由内核协议加一个类型，visor 侧不会为了跑通而造一个只在 Cinux 用的私有类型。鼠标词也没有修饰位，`WireMouse` 里没有对应字段，所以 Cinux 的指针按下与移动都不带修饰键。

## 判例

你打开 `tests/event_tests.cpp`，第一句就是 `static_assert(std::variant_size_v<Event> == 6);`。事件表少一个词或多一个词，编译停在这里，不用等运行。

你顺着滚轮那一段往下读。前面那四步在判例里逐条对，断言盯三件事。在 (2,2) 滚一格时，根收到，目标计数仍是 1，归属不动。在 (200,200) 滚一格时，词被丢掉。在 (2,2) 松手时，抬起仍送给目标，归属才清空。

你接着看按键的修饰键。`KeyDown` 带 shift 与 ctrl，目标收到 65，`ctrl` 与 `shift` 为真，`alt` 为假。`KeyUp` 只带 ctrl，目标收到的仍是同一个 code。Tab 带 ctrl 时焦点从目标移到下一个控件，它的 `key_downs` 是 0，Tab 没有当成普通键送进控件。

我们按按钮逐个查符号。4 给 `dy` 为 -1，5 给 +1，6 给 `dx` 为 -1，7 给 +1，按钮 4 的释放没有词。同一段还查了掩码：按钮 1 按下带 ShiftMask 时 `modifiers.shift` 为真，按钮 4 按下带 ControlMask 时滚轮的 `modifiers.ctrl` 为真、`shift` 为假。滚轮没有单独查位置，按钮 1 的按下位置查了 (12,34)。

你在 `tests/cinux_tests.cpp` 里把 `WireKey` 的 shift 与 ctrl 同时置真，翻译出来的 `KeyDown` 两个标志都为真，`alt` 与 `super` 为假。判例就靠它说明 shift 与 ctrl 的来源、super 的缺席，以及键码走 ASCII 码点这一路。

我们把六个词一起喂给离屏宿主，顺序出队，每个词自己的字段都对得上。滚轮这个词查 `dx` 为 1、`dy` 为 -2、位置为 (9,10)、`ctrl` 为真。同一个文件里还有 `has_clock` 的两条路：空表返回假，离屏宿主的表返回真，而它的时钟读数从 0 开始。

## 边界

我们这一章只把修饰键和轮次时间送到接收者手边。控件还没有一个消费滚轮，没有滚动视口、列表滚动、缩放，也没有拖拽阈值。`PointerMove` 与 `PointerUp` 不带修饰键，这是事件表的形状，不是实现偷懒。`now_ms` 在签名上有了位置，函数体里还没有读者。Cinux 的滚轮要改内核的线上格式，不在 visor 侧补。边界留着，比在适配层造一个只有一边认识的词干净。

## 验收

你构建之后跑测试目标：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build -R visor.tests --output-on-failure
```

你把命令跑完，成功的条件是退出码为 0，屏幕上出现以 `100% tests passed` 开头的那一行。`visor.tests` 通过时不打印任何东西，有失败才会出现 `FAIL 文件:行号 断言`。看到 `FAIL` 行，看它点的是哪个断言，再回头读对应的判例。

我们把完整验收收成一条命令：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

它跑普通构建、ASan/UBSan、TSan、clang-format、clang-tidy 与 fontgen 独立构建六道，任何一道失败就停。你看到最后一行是 `acceptance: all green`，就是六道全过。这一章新增的判例只在离屏对象与纯翻译函数上跑，你不需要显示服务。

两根红绿探针，你可以自己动手做。

第一根，你把滚轮分支改回按归属分发。打开 `src/ui/input_router.cpp`，把 `deliver` 里滚轮那一段的接收者改成 `press_owner != nullptr ? press_owner : root.hit_test(word.pos)`，也就是按住时把词给归属，重新构建。`visor.tests` 会红在「按在目标上、再在 (2,2) 滚一格」那一段，因为根控件收不到词。改回来再跑，退出码回到 0。

第二根，你把修饰键丢掉。打开 `src/bindings/x11/event_wire.cpp`，把 `modifiers_from_state` 里 `.ctrl = (state & ControlMask) != 0` 改成 `.ctrl = false`，重新构建。X11 判例里带 ControlMask 的检查会红。复原再跑，所有判例回到通过。

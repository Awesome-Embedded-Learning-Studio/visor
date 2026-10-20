# 第三十四章 evdev 翻译层：内核词到核心事件

这一章我们给 Linux 输入线补一段纯翻译：把内核的 `input_event` 翻成第三十二章的六个事件词，不打开设备，也不读时钟。你读完能亲手把一段按键流喂进去，看它出什么、丢什么，再改坏两处翻译规则，看判例怎么红。

## 只做纯翻译

Linux 的输入设备住在 `/dev/input/event*`。打开它、读它、用 ioctl 问它支持哪些轴和键，这些事需要文件描述符与内核头，也得有人负责设备发现。我们把这一半留在宿主侧，翻译层只认三个数：类型、代码、值。

这样切开以后，你会看到判例不需要一台带键盘的机器，合成的词就能跑。将来接上一个读设备的适配层，翻译逻辑也不用重写，设备管理留在外面，这一层只管词。

我们把文件放在 `include/visor/bindings/evdev/event_wire.hpp` 与 `src/bindings/evdev/event_wire.cpp`，CMake 里的新库叫 `visor.host.evdev`，只链 `visor.core`：

```cmake
add_library(visor.host.evdev STATIC src/bindings/evdev/event_wire.cpp)
target_link_libraries(visor.host.evdev PUBLIC visor.core)
```

你读这段 CMake 时可以看到它没有链任何宿主库。翻译层与设备读取、显示提交、时钟都不发生关系，这是这一章敢把判例写成纯函数调用的原因。

## WireEvent：只镜像三个字段

内核给的结构叫 `input_event`，里面除了类型、代码、值，还有一组时间字段。我们只镜像前三个：

```cpp
struct WireEvent {
    std::uint16_t type = 0;
    std::uint16_t code = 0;
    std::int32_t value = 0;
};
```

`timeval` 在常见的 32 位 Linux 布局里由两个 4 字节的字组成，到常见的 64 位布局里由两个 8 字节的字组成。另有一种 32 位 time64 布局，会把两个时间字都放宽到 8 字节。我们不读它，镜像它却会把平台的字段宽度带进头文件。时间仍由宿主表的 `clock_ms` 提供，这一层不必知道事件发生在哪一毫秒。

你在 `event_wire.hpp` 与 `event_wire.cpp` 里找不到打开 `/dev/input` 的代码。读设备的适配层归宿主侧，以后才写，它交给翻译层的只有一件事：从内核字里抄出 `type`、`code`、`value`，填进 `WireEvent`，再交给 `translate_event`。设备是谁、读到的是键盘还是鼠标，翻译层不问。

## 常量表

常量照 `linux/input-event-codes.h` 的数值落在 `visor::host::evdev` 下，我们按用途分成几组。正文提内核词时用 `EV_*`、`REL_*`，指代码里的常量时用 `kEv*`、`kRel*`，键与按钮则是 `kKey*` 与 `kBtn*`，两套名字按数值一一对应。事件类型三个：

```cpp
inline constexpr std::uint16_t kEvSyn = 0x00;
inline constexpr std::uint16_t kEvKey = 0x01;
inline constexpr std::uint16_t kEvRel = 0x02;
```

相对轴四个，你读名字时留意 `H` 是 horizontal：

```cpp
inline constexpr std::uint16_t kRelX = 0x00;
inline constexpr std::uint16_t kRelY = 0x01;
inline constexpr std::uint16_t kRelHWheel = 0x06;
inline constexpr std::uint16_t kRelWheel = 0x08;
```

鼠标键从 `0x110` 起。你对着内核的表看，按钮都放在 `0x100` 这一带，键盘码留在下面：

```cpp
inline constexpr std::uint16_t kBtnLeft = 0x110;
inline constexpr std::uint16_t kBtnRight = 0x111;
inline constexpr std::uint16_t kBtnMiddle = 0x112;
```

修饰键的八个编号也在头文件里，名字与数值都写在头文件的常量表里，你读实现时对着看。核心自己命名的键各有一个 evdev 编号。你对照数值：Backspace、Tab、Enter、Space 本来就与核心相同，方向键的编号与核心不同。

```cpp
inline constexpr std::uint16_t kKeyBackspace = 14;
inline constexpr std::uint16_t kKeyTab = 15;
inline constexpr std::uint16_t kKeyEnter = 28;
inline constexpr std::uint16_t kKeySpace = 57;
inline constexpr std::uint16_t kKeyUp = 103;
inline constexpr std::uint16_t kKeyLeft = 105;
inline constexpr std::uint16_t kKeyRight = 106;
inline constexpr std::uint16_t kKeyDown = 108;
```

## EvdevState：屏幕、指针与修饰位

有些状态不在每个词里，翻译层得自己攒：指针现在在哪，哪些修饰键还按着。我们让 `EvdevState` 装这两样，外加构造时传进来的屏幕尺寸：

```cpp
class EvdevState {
  public:
    explicit EvdevState(base::SizeI screen) noexcept;

  private:
    friend std::optional<host::Event> translate_event(EvdevState& state,
                                                      const WireEvent& wire) noexcept;

    void set_modifier(std::uint16_t code, bool pressed) noexcept;

    base::SizeI screen_;
    base::PointI pointer_;
    host::Modifiers modifiers_;
    base::bit::BitMask<std::uint8_t> held_modifiers_;
};
```

指针从 `(0, 0)` 起，也就是屏幕左上角。你读 `REL_X`、`REL_Y` 时留意它们每个词只动一个轴，累加完再按屏幕夹取。夹取在 64 位里做加法，免得一个来路不明的超大值在比较之前就溢出：

```cpp
int clamped_axis(int current, std::int32_t delta, int extent) noexcept {
    if (extent <= 0) {
        return 0;
    }
    const std::int64_t next = static_cast<std::int64_t>(current) + delta;
    const std::int64_t last = static_cast<std::int64_t>(extent) - 1;
    if (next < 0) {
        return 0;
    }
    if (next > last) {
        return static_cast<int>(last);
    }
    return static_cast<int>(next);
}
```

你把 `extent` 读成屏幕宽或高，合法的最大坐标是 `extent - 1`。空屏幕进来，每个方向都夹到 `(0, 0)`，不会出现负尺寸的边界。

我们把修饰键按物理键记位，左右两半各占一位。位运算走 `base::bit` 的词：`mask_bit` 造掩码、`bit_at` 取位、`deposit` 写值、`has` 查位，没有新写裸掩码。写完之后再从两半的掩码合成四个命名标志：

```cpp
void EvdevState::set_modifier(std::uint16_t code, bool pressed) noexcept {
    const std::uint8_t position = modifier_position(code);
    if (position == kNoModifierPosition) {
        return;
    }
    held_modifiers_.deposit(base::bit::bit_at(position),
                            pressed ? std::uint8_t{1} : std::uint8_t{0});
    modifiers_.shift = held_modifiers_.has(modifier_mask(kLeftShiftPosition, kRightShiftPosition));
    modifiers_.ctrl = held_modifiers_.has(modifier_mask(kLeftCtrlPosition, kRightCtrlPosition));
    modifiers_.alt = held_modifiers_.has(modifier_mask(kLeftAltPosition, kRightAltPosition));
    modifiers_.super = held_modifiers_.has(modifier_mask(kLeftMetaPosition, kRightMetaPosition));
}
```

左右 Shift 分成两位，是为了重叠时不丢状态。你同时按住左右 Shift，再松开左手，`shift` 仍为真，因为右 Shift 的位还在。要是两半共用一个 bool，松开一只手就会把另一只手也抹掉。判例里专门有一组做这件事。

## 翻译规则

我们只留一个入口，签名在头文件里：

```cpp
[[nodiscard]] std::optional<host::Event> translate_event(EvdevState& state,
                                                         const WireEvent& wire) noexcept;
```

我们按类型分派。`EV_SYN` 是同步分隔词，丢掉。类型不认识，丢掉。`EV_REL` 里轴不认识，也丢掉。

`EV_KEY` 的值我们只认 0、1、2。1 是按下，0 是抬起，2 是内核的自动重复。重复仍出 `KeyDown`，因为要不要响应重复是控件的判断，翻译层不替它过滤。修饰键在发自己的事件之前更新状态，于是 Shift 的按下事件自己就带着 `shift`，它后面的 A 也带着。

鼠标键走同一条 `EV_KEY` 分支。你把按钮号读成 1 到 3，位置取跟踪到的指针坐标，按下时带当前修饰键。按钮的重复值 2 丢掉。`0x100` 以上不认识的按钮也丢掉，不冒充键盘键。抬起只带按钮号与位置，`PointerUp` 本来就没有修饰键字段。

我们让键盘码默认按 evdev 原码透传。唯一的改号是方向键：evdev 的 103、105、106、108 落在可打印 ASCII 里，直接透传会与文本约定冲突，所以翻译层把它们映到核心的 `0x110` 到 `0x113`。映射函数只有四个分支：

```cpp
std::optional<std::uint32_t> named_key(std::uint16_t code) noexcept {
    switch (code) {
        case kKeyUp:
            return host::kKeyUp;
        case kKeyDown:
            return host::kKeyDown;
        case kKeyLeft:
            return host::kKeyLeft;
        case kKeyRight:
            return host::kKeyRight;
        default:
            return std::nullopt;
    }
}
```

`EV_REL` 的每个词出一条事件。你数一下：X 一个词出一条 `PointerMove`，Y 再来一个词再出一条，所以内核连发 X、Y 两个词，核心看到的是两次移动。滚轮同样一个词一条：`REL_WHEEL` 的正值表示向上，核心约定向上是负的 `dy`，所以要取反。`REL_HWHEEL` 向右是正的 `dx`，符号不用动。

你取反时会碰到一个边界。`INT32_MIN` 取反在二进制补码里溢出，`negated_steps` 把它饱和到 `INT32_MAX`。判例会专门喂一次这个值。

你按格数读这个值：单位仍是第三十二章的格。一个词里写 2 就是两格，仍然只出一条事件，`dy` 等于负二。

我们不让这一层生产文本。字母和数字保持 evdev 原码。空格在 evdev 里是键码 57，字符 32 才是空格本身的码点。文本控件若按码点读 57，会把它当成字符 `9`。键位表属于翻译之后的另一层，这一章不提供。

## 翻译结果为空为什么用 optional

你可以把返回空当成正常结果，不是失败：`EV_SYN`、未知类型、未知轴、越界值，对调用方来说动作完全一样，丢掉这一词往下走。给一个没有第二种处理方式的错误通道，只会让调用方多写一个永远走不到的错误分支。

你要知道唯一本该被区分的是 `SYN_DROPPED`，也就是 `EV_SYN` 带 code 3。它表示内核已经丢过事件，跟踪的指针和修饰键可能过期，正确的反应是重建状态。这一层把它按普通同步词丢掉，不重建。这个缺口写在头文件的注释里，也在判例里明着验一次：code 3 与 code 0 一样返回空。缺口没有藏在实现里。

## 判例

判例住 `tests/evdev_tests.cpp`，走的是合成词，不打开任何设备。你打开文件会看到 `key(code, value)` 与 `rel(code, value)` 两个造词的帮手，`step` 把造好的词喂给一个 `EvdevState`。

第一组验修饰键。你跟着看：Shift 按下、A 按下、A 重复、A 抬起、Shift 抬起，逐条查修饰位。八个物理修饰键各按一次、松一次。最后是重叠：

```cpp
EvdevState state(SizeI{.w = 10, .h = 10});
static_cast<void>(step(state, key(kKeyLeftShift, 1)));
static_cast<void>(step(state, key(kKeyRightShift, 1)));
const auto release_left = step(state, key(kKeyLeftShift, 0));
const auto* left_word = as_key_up(release_left);
VISOR_CHECK(left_word != nullptr);
if (left_word != nullptr) {
    VISOR_CHECK(left_word->modifiers.shift);
}
```

第二组验八个核心键。你把 Backspace、Tab、Enter、Space 与四个方向键各按一次，逐个数对核心码。

第三组验移动。你让指针逐轴累加，四个方向都夹到边界，末尾再喂一次 `INT32_MAX` 与空屏幕。

第四组验按钮。你看跟踪到的位置，按钮 1 到 3 各按一次，确认按下带修饰键，再看重复值与 `0x113` 的未知按钮被丢掉。

第五组验滚轮。你拨上下左右四个方向，一格与两格各来一次，末尾再喂一次 `INT32_MIN` 看饱和。

最后一组验丢弃。你把 `EV_SYN` 的 code 0 与 3、未知类型 `0x99`、未知轴 `0x2f` 与 `EV_KEY` 的值 7 逐条喂进去，看翻译结果都为空。

## 这一层没做的事

我们把它挡在 `/dev/input` 外面：它不打开设备，不做 ioctl，不发现设备。它不处理 `EV_ABS` 与多点触控，绝对坐标归触摸屏那一层。它不重建 `SYN_DROPPED` 之后的状态。它不生产文本，也没有键位表。翻译层只回答「这个词翻成哪个核心事件」，翻不出来就丢掉，设备、时钟、文本各归各的层。

## 验收

你单独跑一遍测试可执行文件：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 4
./build/tests/visor.tests
```

你跑通时看不到任何输出，退出码为 0。任何一个判例失败，测试框架会打印 `FAIL tests/evdev_tests.cpp:行号 表达式`，退出码变成 1。

整份验收我们用一条命令跑完，六道依次跑：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

你会看到最后一行打印 `acceptance: all green`，退出码为 0。中间任何一道红了，脚本立即停下，不会跳过。

第一个实验，你把 `event_wire.cpp` 里 `modifier_position` 的 `kKeyRightShift` 分支改成返回 `kLeftShiftPosition`，让左右 Shift 共用一位。重新构建跑测试，左右 Shift 重叠那一组会红，因为松开左手后 `shift` 变成假了。把分支改回去，恢复绿。

第二个实验，你把 `named_key` 的四个方向键分支删掉，让它们按原码透传。重新构建跑测试，八个核心键那一组会红，方向键的期望是 `0x110` 到 `0x113`，收到的是 103、105、106、108。改回去，恢复绿。

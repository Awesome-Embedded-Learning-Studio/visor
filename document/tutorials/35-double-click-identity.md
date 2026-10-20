# 第三十五章 双击与控件身份

第三十四章把内核的输入词接到了六个事件上，第三十二章给分发带上了每轮读一次的时钟。这一章做这些事件之上的第一件手势：双击。事件表不加词，单击也不等：每一对按下抬起照旧立刻送 `on_pointer_up`，双击只是额外再送一次 `on_double_click`。为了在两次点击之间认出同一个控件，我们还要给控件发编号。你读完能把 500 改成 501，看边界判例怎么红。

第三十二章读时钟用的是 `current_time_ms(host)`，查有没有时钟留的是 `has_clock(host)`。到这一章，读取与查询已经换成成员函数 `host.now_ms()` 与 `host.has_clock()`。语义没有变，名字换了，你读第三十二章的调用时把旧名字对到成员函数上。

## 单击不等待

你会在 `Widget` 里看到新增的回调，它只有两个参数：

```cpp
virtual void on_double_click([[maybe_unused]] base::PointI at,
                             [[maybe_unused]] host::Modifiers modifiers) noexcept {}
```

你把位置读成第二下抬起的位置，把修饰键读成第二下按下时的修饰键。默认实现是空的，不关心双击的控件什么都不用改。

你会在这里读反一次：双击不是把两次单击合成一次。第一下抬起照常触发 `on_pointer_up`，第二下抬起也照常触发，然后 `InputRouter` 再补一次 `on_double_click`。一个把每次点击都当动作的按钮，到双击回调发生时已经动作了两次。这个代价是明着接受的，换来的是单击永远不延迟。

要是反过来做，`InputRouter` 就得把单击压住，等过 500 毫秒确认没有第二下再放行，每个按钮都要多一个计时器与一份待发动作。我们不让这一层背这个包袱。真需要「单击选中、双击打开」的控件，你把决定权留给控件自己，由它处理已经发生的两次单击。

## 一次点击长什么样

双击由两次点击组成，所以我们要把一次点击定义清楚。三个条件同时成立才算：按下与抬起都是鼠标左键（按钮 1），按下与抬起都属于同一个控件，抬起落在该控件的 `bounds()` 内。

你把归属想成第二十章的捕获语义。按下时命中测试定主，之后的移动与抬起都归这个主，不管指针滑到哪里。按下落在空处就无人为主，后面那次抬起也到不了任何人手里。判例里「按空处」那一组就是在按下时让待配对的第一次点击失效的。

你停一下看抬起处理的顺序。它算落点是否在界内，算完再调用控件的 `on_pointer_up`：

```cpp
Widget* owner = state.press_owner;
const std::uint32_t press_button = state.press_button;
const host::Modifiers press_modifiers = state.press_modifiers;
state.press_owner = nullptr;
state.press_button = 0;
state.press_modifiers = host::Modifiers{};
const bool inside = owner != nullptr && owner->bounds().contains(word.pos);
if (owner != nullptr) {
    owner->on_pointer_up(word.pos);
}
```

为什么不在回调之后算？因为回调可能把控件挪走或改尺寸。一个按钮在松手时把自己移开，按松手后的框来判，界内的点击会被算成界外。我们按松手那一刻的框判定，然后让回调去改状态。

我们在按下时还存了两样东西：按钮号与修饰键。抬起事件 `PointerUp` 只有按钮号和位置，没有修饰键字段，所以第二下按下的修饰键必须在按下时抄下来，留到抬起时用。按钮号同理，按下按的是右键、抬起却报左键，这一对不算点击。

## 双击窗

我们把两个常数放在 `InputRouter` 里，你从任何调用方都能读到：

```cpp
static constexpr std::uint64_t kDoubleClickWindowMs = 500;
static constexpr int kDoubleClickSlopPx = 4;
```

我们要求两次点击的释放时间相差不超过 500 毫秒，每轴位置相差不超过 4 像素，才算一对。两个上限都含边界，500 算，501 不算，4 像素算，5 像素不算。位置差在 64 位里算，免得两个极端坐标在比较之前溢出：

```cpp
bool within_click_slop(base::PointI first, base::PointI second) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(first.x) - second.x;
    const std::int64_t dy = static_cast<std::int64_t>(first.y) - second.y;
    constexpr std::int64_t k_slop = InputRouter::kDoubleClickSlopPx;
    return dx >= -k_slop && dx <= k_slop && dy >= -k_slop && dy <= k_slop;
}
```

我们让时间差走无符号减法，跟第八章的时钟约定一致：

```cpp
if (state.has_last_click && state.last_click_id == owner->id() &&
    state.now_ms - state.last_click_ms <= InputRouter::kDoubleClickWindowMs &&
    within_click_slop(state.last_click_pos, word.pos)) {
    state.has_last_click = false;
    owner->on_double_click(word.pos, press_modifiers);
    return;
}
```

你在判例里会看到第一次释放放在 `UINT64_MAX - 100`，第二次放在 200。绝对值上第二次小得多，无符号差仍是 301 毫秒，这一对成立。要是写成 `now > then` 再相减，回绕那一下就会把新点击判成旧点击。

你还要看时钟的前提。宿主表可以没有 `clock_ms`，`host.has_clock()` 为假。这时 `host.now_ms()` 读出来是 0，而 0 是一个真实的毫秒数，不能拿来当「没有时钟」的标记。所以双击比较还要一个独立的 `clock_available`，它为假时双击整个关掉。判例在没有时钟的条件下点两下，确认不出双击，再打开时钟点一对，确认恢复正常。

## 什么时候把待配对清掉

我们让 `InputRouter` 把第一次合格点击记下来，等第二次来配。下面这些情况会把它清掉：

- 右键的抬起。左键要求按下与抬起都是 1，右键那一对里两个都不是。
- 拖出控件的抬起。归属仍收到 `on_pointer_up`，但 `inside` 是假，这一对不算点击。
- 按在空处的按下。命中测试没人，按下那一刻就把待配对标记置假，不用等抬起。
- 按下与抬起的按钮号不一致。判据是 `press_button == 1 && word.button == 1`，两个都要成立。
- 宿主没有时钟。`clock_available` 为假时，释放既不记新的待配对，也把手上的清掉。

你换一个控件点击，记录会被直接替换，不会清掉后重来。第一次点击在 A，第二次在 B，两次不配对，但 B 这次会成为新的待配对。再点一次 B，就与它配成一对。判例里 A 点一次、B 点一次，两边都不出双击，第三下点 B 才出。

我们让一对只出一次双击。配对成功时 `has_last_click` 被置假，第三下点击重新起算，第四下才和第三下配成第二对。三连击的判例验的就是这个：三次点击只出一次双击，第四下再出一次。

## 修饰键从第二下取

你读回调签名时会看到，回调报的是第二下按下时的修饰键。第一下按着 Shift，第二下按着 Alt，报的就是 Alt。原因前面提过：抬起事件不带修饰键，`InputRouter` 只能在按下时抄。判例专门把两下按成不同的修饰键，确认回调拿到的是后一个。

## 身份：把地址换成编号

你要让双击记录活过好几轮。第一下点击在第三轮，第二下在第七轮，中间控件可能被摘除、销毁，同一块内存又分给一个新控件。要是记录里存的是控件地址，新控件会继承旧控件的待配对点击，在同一个位置点一下就莫名其妙地双击了。

我们给控件一个进程内唯一的编号。编号住在 `include/visor/base/identity/id_session.hpp`，文件里定义了三个概念：

```cpp
template <typename T>
concept IdValue = std::unsigned_integral<T> && !std::same_as<T, bool>;

template <typename T>
concept AtomicIdValue = IdValue<T> && requires { requires std::atomic<T>::is_always_lock_free; };

template <typename Generator, typename T>
concept IdGenerator = requires(Generator& generator) {
    { generator.next() } noexcept -> std::same_as<T>;
};
```

你读上面三个概念：`IdValue` 排除 `bool`，因为 `bool` 在类型特征里算无符号整数，却只有两种取值，撑不起一个身份空间。`AtomicIdValue` 再要求原子量无锁，目标平台的原子量要是需要运行期加锁，编译就失败，不会把锁悄悄藏进生成器。

我们把默认生成器写成一个从 1 开始的原子计数：

```cpp
template <AtomicIdValue T>
class AtomicIdGenerator {
  public:
    explicit constexpr AtomicIdGenerator(T first = T{1}) noexcept : counter_{first} {}

    [[nodiscard]] T next() noexcept {
        T id = counter_.fetch_add(T{1}, std::memory_order_relaxed);
        if (id == T{0}) {
            id = counter_.fetch_add(T{1}, std::memory_order_relaxed);
        }
        return id;
    }

  private:
    std::atomic<T> counter_;
};
```

我们故意让 `fetch_add` 用 `memory_order_relaxed`。编号只是一个标签，不承担同步职责，不需要拿它给别的内存操作排顺序。零被保留给「没有」，环绕时跳过零再取一个。

我们把 `IdSession` 写成只转发，它自己不做判断：

```cpp
template <IdValue T, IdGenerator<T> Generator = AtomicIdGenerator<T>>
class IdSession {
  public:
    IdSession() = default;

    template <typename... Args>
        requires std::constructible_from<Generator, Args...>
    explicit IdSession(Args&&... args) noexcept : generator_(std::forward<Args>(args)...) {}

    [[nodiscard]] T next() noexcept { return generator_.next(); }

  private:
    Generator generator_;
};
```

你拿到的每个实例就是一个身份空间。两个实例各发各的，都会从 1 起，这没问题，因为它们服务的对象本来就该各自编号。构造参数原样转给生成器，所以你换一个走步长或走固定序列的策略，`IdSession` 一个字都不用改。判例里的 `TensGenerator` 每次加 10，`StepGenerator` 的起始值 100 经 `IdSession` 转进去，就是这个口子的用法。

我们给控件用进程级实例，住在 `widget.cpp` 的匿名命名空间：

```cpp
constinit visor::base::IdSession<std::uint32_t> widget_ids;

Widget::Widget(base::RectI bounds) noexcept : bounds_{bounds}, id_{widget_ids.next()} {}
```

你可以把 `constinit` 读成「静态初始化阶段就定下来」，它不会和别的翻译单元里的全局控件抢初始化顺序。`Widget::id()` 只读，构造之后不再变。32 位编号意味着要造出四十多亿个控件才绕回一次，一个进程碰不到。

我们让待配对点击存这个编号，不存地址。判例里把旧控件从树上摘掉、让它析构，再在同一个树位置造一个新控件，点同样的坐标。栈上的旧槽位很可能被它复用，这正是地址会重复的场景。编号不同，旧记录不认它，第一次点击不出双击，第二次才与它自己配成一对。

## 分发签名跟着长

双击判定要用时钟，还要知道时钟是否存在。你会在 `dispatch_event` 的参数里看到多出来的那个 `bool`：

```cpp
void dispatch_event(Widget& root, InputRouter& input, const host::Event& event,
                    std::uint64_t now_ms, bool clock_available) noexcept;
```

你读 `run_dirty` 与 `run_once`，它们每轮读一次 `now_ms`，再把 `host.has_clock()` 随每个事件一起传给 `dispatch_event`。同一轮里的事件共用这个时间读数，双击的两次释放通常落在不同轮，差值就是两轮之间的毫秒数。

我们让 `InputRouter` 新存了按下按钮与修饰键，还有上次点击的编号、位置、时间，以及一个待配对标志。投递函数不收整个 `InputRouter` 引用，`Delivery` 这个投递参数包把这些字段按引用带进去，这些状态仍只由 `dispatch_event` 写入。`dispatch_event` 是 `InputRouter` 的友元，别的代码只能读 `press_owner()` 与 `focus_scope()`，改不了待配对状态。

## 判例

你看 `tests/id_session_tests.cpp`，它验编号序列：默认从 1 起，两个实例各自从 1 起。`uint8` 的 255 之后是 1，`uint16` 的 0 起手也跳过零，`uint64` 照常计数。两个自定义生成器按各自的步子走，概念静态断言逐条成立。

你再看 `tests/double_click_tests.cpp`，它用一个只数回调的 `ClickSpy`，把上面每一条规则都走一遍。普通一对与窗口边界各有一组，500 与 501 分两边。换控件一组，每轴 4 与 5 像素也各有一组。右键、无时钟、时钟回绕各有一组，拖出控件与按空处也各有一组。三连击与身份也在里面。身份那一组是这一章独有的：

```cpp
{
    ClickSpy first(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
    VISOR_CHECK(desktop.add_child(&first));
    click(desktop, input, PointI{5, 5}, 100);
    VISOR_CHECK(desktop.remove_child(&first));
}
{
    ClickSpy second(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
    VISOR_CHECK(desktop.add_child(&second));
    click(desktop, input, PointI{5, 5}, 200);
    VISOR_CHECK_EQ(second.doubles, 0);
    click(desktop, input, PointI{5, 5}, 250);
    VISOR_CHECK_EQ(second.doubles, 1);
}
```

你在两段之间会看到上面说的槽位复用：旧控件在第一段末尾析构，新控件在第二段开头构造。编号不同，第二段的第一次点击不与第一段的记录配对。

## 边界

我们把这一章的范围限定在双击。三击、长按、拖拽阈值各是各的手势，不在这里。

你接受这个代价：一个同时想要单击与双击的控件，会收到两次单击，之后才轮到双击。控件要不要等、怎么等，是控件自己的策略。

你留意按压归属的 `press_owner_` 仍是裸指针，窗口在按住时销毁，仍需调用方收尾。按压归属与双击无关，但别把它和编号的寿命混在一起。

我们把按钮参数限定在 1。其他键不参与双击，也不阻止它们走原有的按下与抬起。

你看判例，它走的是合成的指针事件，X11 窗口里的实际点击没有单独判例。分发路径是同一条，但这一层没在 X11 窗口里点过。

## 验收

你跑一遍测试可执行文件：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 4
./build/tests/visor.tests
```

你跑通时看不到任何输出，退出码为 0。失败时测试框架会打印 `FAIL tests/double_click_tests.cpp:行号 表达式`，退出码变成 1。

整份验收我们用一条命令跑完：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

你会看到最后一行打印 `acceptance: all green`，退出码为 0。六道检查从 rel 构建与全部测试开始，最后一道是 fontgen 的独立构建。中间任何一道红了，脚本立即停下。

第一个实验，你把 `input_router.hpp` 里的 `kDoubleClickWindowMs` 从 500 改成 501，重新构建跑测试。500 仍然算一对，501 会变成一对，判例期望它不算，于是红。改回 500，恢复绿。

第二个实验，你把 `widget.hpp` 里 `id()` 的返回改成常量 0，等于让所有控件共用一个编号，模拟地址被复用。重新构建跑测试，身份那两组会红：两个控件的编号会变得相同，被销毁的旧控件也会把记录交给新控件。改回 `return id_;`，恢复绿。

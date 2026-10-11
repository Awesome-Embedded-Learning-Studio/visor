# 第十章 焦点链与复选框：键盘进场，控件长出开关

第九章把指针的闭环走通了，按下、翻转、像素跟着变。键盘事件却从第二章起一直躺在词表里没人接，分发器收到键按下，看一眼，放走。这一章（B4）让键盘第一次有了去处，Tab 在控件树里移动焦点，焦点落在谁身上，谁就被画出一圈一像素的边框。顺路长出第三种控件，复选框，第一个带开关状态的。同一批还做了一件屋子里的活，宿主表和它的测试替身分了家。这一章照旧有真实的红：一次是回绕逻辑的红绿实验，一次是我把画布底色当成了控件的绘制范围。

## 焦点住在树上

焦点是什么？键盘同一时刻只能对一个控件说话，哪个控件在被说话，这个事实得有人记着。放在哪，有三个候选。

放一个全局管理器，等于在树之外另立一张平行表，树一动它就得跟着同步，两边迟早对不上。放运行循环里，焦点却要跨轮存活，循环本身恰恰是每轮从零开始的。剩下第三个位置，控件树自己：每个控件带一个 `bool`，整棵树至多一个为真。

```cpp
[[nodiscard]] bool focused() const noexcept;
void set_focused(bool on) noexcept;
```

「至多一个」这条不变量谁来守？`set_focused` 是公开的，单看它守不住，调用方完全可以点亮两个。守它的是移动焦点的那一个函数 `advance_focus`，它先把旧的熄掉，再把新的点亮，两步之间不变量不悬空。这与第九章孩子容量的口径一致：函数负责维持承诺，不指望每个调用方都记得。

哪类控件能接焦点，是控件自己的事，所以是一个虚函数，默认拒绝：

```cpp
[[nodiscard]] virtual bool focusable() const noexcept { return false; }
```

基类不接输入，第九章在指针接口上说过一次，这是键盘版的同一句话。

## Tab：第一个被点名的键码

第二章的 `events.hpp` 有一句注释：键码是宿主定义的，核心只比较码值，从不解码。Tab 导航要动这条吗？要动，而且只动一点。

焦点移动是树自己的语义，分发器必须认出「移动」这个命令，否则键盘什么也驱动不了。于是 `events.hpp` 多了一个具名常量：

```cpp
inline constexpr std::uint32_t kKeyTab = 15;
```

注释写明这是当时唯一的例外：焦点遍历是树自己的语义，分发器必须认识这把钥匙，其余键码照旧宿主定义、照旧不透明。值取 15 是 evdev 的 Tab 键码，PC 宿主把平台键码映射过来就是它。第十二章会让 Enter 与 Space 以同样的理由进场，那是后话。

有没有更干净的绕法？有，让宿主把键码翻译成语义事件再进核心。但那要为一把钥匙新增一层事件词，而规格说的是五个事件词，手势与文本输入都长在它们之上。具名一个值是同一思路的最小版本，词汇不扩，点名一个。

## advance_focus：第三种走树

一棵树到现在有三种走法。绘制走先序，父先子后。命中走逆序，后加者先问。焦点移动按先序找「下一个可聚焦的」，实现分两步：先在树里找当前持有者，再从它往后找下一个可聚焦的控件，走到头没有就回绕到第一个。

```cpp
// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
Widget* Widget::find_focusable_after(Widget& node, Widget* current, bool& seen_current) noexcept {
    if (!node.visible_) {
        return nullptr; // A hidden branch takes no focus, matching draw() and hit_test().
    }
    if (node.focusable() && seen_current) {
        return &node;
    }
    if (&node == current) {
        seen_current = true;
    }
    for (std::size_t i = 0; i < node.child_count_; ++i) {
        if (Widget* next = find_focusable_after(*node.children_[i], current, seen_current);
            next != nullptr) {
            return next;
        }
    }
    return nullptr;
}
```

递归又来了，理由与绘制、命中同一条：真实界面树深三四层，显式栈要多发明一个规格里没有的深度常量。藏起来的分支直接跳过，与绘制、命中是同一口径，看不见的控件不接键盘。

回绕有一个要紧的边角。整棵树只有一个可聚焦控件时，回绕会找回它自己，焦点原地不动。树用返回值把这个事实说出来，不另设错误通道：

```cpp
bool Widget::advance_focus() noexcept {
    Widget* current = find_focused_in(*this);
    bool seen_current = (current == nullptr);
    Widget* target = find_focusable_after(*this, current, seen_current);
    if (target == nullptr && current != nullptr) {
        // Wrap: nothing focusable after the holder, restart from the top. The
        // restart may land back on the holder itself, which is the tree saying
        // it has exactly one focusable widget, and focus stays there.
        seen_current = true;
        target = find_focusable_after(*this, nullptr, seen_current);
    }
    if (target == nullptr) {
        return false;
    }
    if (current != nullptr && current != target) {
        current->set_focused(false);
    }
    target->set_focused(true);
    return true;
}
```

空树、或者满树没有一个可聚焦控件，返回 false，什么都不动。分发器对这个返回值也不在意，Tab 照常算作已消耗的事件。

## 焦点环是框架的策略

焦点看不见，键盘用户就迷路。反馈是一圈一像素的边框，画在边界内侧。交给每个控件自己画？第二个控件就会忘。所以 `draw()` 在 `on_draw` 之后统一画：

```cpp
on_draw(painter);
if (focused_) {
    paint_focus_border(painter, bounds_);
}
```

顺序有意如此。按钮把整个边界填满，环必须压在内容上面才看得见，反过来就要求每个控件先给自己留边，忘了那一留，环就没了。焦点反馈是框架的策略，不是控件的喜好。

控件侧只剩一条配合纪律：内容别顶到边界。复选框把框体缩进两像素，一行给环，再留一行亮边隔开环与框两条暗线。不留这行亮边，环和框并成一条粗边，焦点信息就糊了。

## Checkbox：按压即翻转

第三种控件，第一个带开关状态的。全部状态一个 `bool`，按下翻转，抬起不动。

翻转放在按下还是抬起？桌面习惯是抬起，点击完成才算提交。触摸屏习惯是按下。我们的分发器没有点击合成，事件词只有按下和抬起两个，「用户点了一下」这个复合意思不存在。所以按一下就翻，是对手头两根事件词最老实的映射，也正好是触摸屏的手感。抬起于是无事可做，`Checkbox` 只覆写 `on_pointer_down`，这一点与按钮不同，按钮的抬起要复原暂态，复选框没有暂态可复原。

绘制一共两笔：

```cpp
painter.fill(box, canvas::Color::Off);
if (!checked_) {
    painter.fill(inner, canvas::Color::On);
}
```

框体永远填暗，未勾选时内部填回亮色，勾选时内部留着不动。勾选是实心方块，未勾选是一圈细框，状态差异全部落在第二笔上。框体比边界小一圈的时候 `box` 会算出空矩形，第五章的笔对空矩形是静默跳过，这里不需要特判。

## 宿主表分家

同批的屋子里的活。`bindings/host.hpp` 从第六章起住着两户人家，核心侧的表（`HostTable`、`FlushRegion` 与三个入口函数）和 PC 侧的测试替身（`OffscreenHost`、`write_pbm`）。替身体内有 `std::vector`。规格的标准库白名单里没有 vector，核心侧的头本不该沾它，可替身一住进来，裸机构建和将来的 Cinux 内核构建只想要表定义，也得把 vector 拖进编译。

分家。`host.hpp` 留核心侧，此后包含的只有几何词汇、事件词和 `<cstddef>`、`<cstdint>`。替身与 PBM 写出器搬去新的 `offscreen_host.hpp`，头注释写明这是 PC 半边，替身的堆容器是有记录的例外，不许再漏回核心侧头。`run_loop.hpp` 只包含 `host.hpp`，这条缝从此干净。

这件事的来历值得一记。作者拿了四个问题去问另一个模型，回来我们逐条交叉核对，host.hpp 混装是对方看见而我们没有看见的两处之一，另一处是分叉留档清单，同样采信，落在了 brief 末尾。别人家的路线图能照出自己家的死角，这件事本身就是教程的一部分。

## 测试的几个回合

焦点组用三个按钮排成加入顺序：Tab 按绘制顺序走，第一个落在大哥头上，再按轮到二哥，走完老三回绕到大哥。藏起二哥，Tab 跳过它。把持有者本尊藏起来，焦点挪到第一个可见的可聚焦控件。一整棵 `Rectangle`，没有一个可聚焦的，`advance_focus` 返回 false，原地不动。

像素判例给得细，因为焦点信息就住在那几行像素里。勾选且持焦点的复选框，自上而下四行各司其职，环是暗的，亮边是亮的，框线是暗的，标记是暗的。四行断言一条一条读下去，就是视觉层级本身。

红过一次，实录。复选框第一版像素断言里，我写了框外角上读亮色。跑起来是暗的。控件只画自己的框，框外那颗像素是画布底色，底色默认是暗的。背景归场景管，不属于控件的绘制承诺，把画布底色改成 `set_clear(On)`，断言就绿了。这一红教的事：判例里的背景是场景给的，不是控件给的，写像素断言之前先想清楚每颗像素是谁画的。

可重放承诺顺手扩了容。面板上压一个按下状态的按钮，再加一个勾选且持焦点的复选框，画两遍逐字节比对。焦点环和标记一起进了「画与状态分离」的看守范围，哪个控件的绘制偷偷改了状态，这个测试当场红给你看。

## 验收

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

全部测试绿之后，跑测试程序看判例输出：

```bash
./build/tests/visor.tests && echo ok
```

红绿实验做一次回绕的。打开 `src/ui/widgets/widget.cpp`，找到 `advance_focus` 里的回绕块：

```cpp
    if (target == nullptr && current != nullptr) {
        // Wrap: nothing focusable after the holder, restart from the top. The
        // restart may land back on the holder itself, which is the tree saying
        // it has exactly one focusable widget, and focus stays there.
        seen_current = true;
        target = find_focusable_after(*this, nullptr, seen_current);
    }
```

把条件临时改成恒假，重新构建再跑，`widget_tests` 里回绕断言与「持有者被藏起来」的判例一起转红。回绕没了，Tab 走完最后一个可聚焦控件后就无处可去，而树里藏着持有者的那一格，也再没有出口。看完复原，绿。

想再看一条的话，把 `run_loop_tests` 里 Tab 回合的两个按钮换成两个 `Rectangle`，Tab 照常被消耗、照常计数，但两条焦点断言转红。可聚焦是控件自己的声明，分发器不会替它猜。

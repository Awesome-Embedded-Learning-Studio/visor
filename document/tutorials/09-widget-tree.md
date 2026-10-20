# 第九章 控件树与运行循环：核心长出第一棵界面

前八章搭好了底座：像素画得出来，事件收得进来，时间读得到。这一章（B3）让这些东西第一次连成一个闭环——指针按下，控件状态翻转，下一帧像素跟着变。控件树、两个控件、一条循环，都是从零写的，测试先行一步没跳。这一章里你会看到两次真实的绕路：一次是接口画不出来被迫返工，一次是命中顺序的测试期望写错被真相纠正。

## 控件为什么是运行时对象

规格里有一条分工：几何词汇、位布局、画布这些走模板，控件状态、焦点、页面这些走运行时对象。理由不难想。画布的宽高在类型里（`Canvas<SizeI{20, 20}>`），编译期就知道每个字节落在哪，运行期一分钱都不用花。控件不一样，它的状态随事件变：按下、抬起、显示、隐藏。要是把状态也做成模板参数，每换一次布局就要重新编译一遍程序，界面就成了幻灯片。

所以控件用类继承加虚函数。仓库禁 RTTI（`-fno-rtti`），禁的是 `dynamic_cast` 和 `typeid` 这类运行期类型识别，虚函数本身不沾边：虚表是编译期建好的普通跳转表，不需要知道「这个指针到底是什么类型」这种问题。控件多态走虚函数，这条路规格明文放行。

## Painter：一道缝的诞生

写第一个控件测试之前，先对着 `pen.hpp` 和 `canvas.hpp` 核对接口，然后发现计划里的控件绘制签名根本写不出来。

计划草图是这么想的：

```cpp
void on_draw(draw::Pen<canvas::MonoCanvas>& pen) const noexcept;
```

`canvas::MonoCanvas` 这个类型不存在。画布类型叫 `Canvas`，而且宽高在模板参数里，`Canvas<SizeI{13, 5}>` 和 `Canvas<SizeI{20, 20}>` 是两个不同的类型。控件要是拿具体画布类型当参数，整棵控件树就被锁死在一个屏幕尺寸上。控件要是自己变成模板，就违背了运行时对象这条规格。两条路都不通，说明问题出在问题本身：控件不该直接认识画布，中间缺一层。

缺的这层叫 `Painter`（`include/visor/ui/painter.hpp`），一个只有两个虚函数的接口：

```cpp
class Painter {
  public:
    virtual void fill(base::RectI area, canvas::Color ink) const noexcept = 0;
    virtual void plot(base::PointI at, canvas::Color ink) const noexcept = 0;

  protected:
    ~Painter() = default;
};
```

控件只认识这个接口，永远不知道背后是哪个画布。绑定的活交给一个模板适配器 `CanvasPainter<CanvasT>`（`include/visor/ui/canvas_painter.hpp`）：它借来一个画布，每次落笔临时造一支 `Pen`，把调用原样转发过去。第五章的越界静默、半开矩形语义，全都留在笔那一层，一行没动。

这就是这一章的接口决定：模板层（笔、画布、几何）和对象层（控件）各自成立，`Painter` 是两层之间唯一的缝。跑循环的代码里有画布，它造一个 `CanvasPainter`，以 `const Painter&` 的身份递给控件树。控件从头到尾不知道画布的存在。

析构函数是 protected 而且不虚，这也是有意的：控件树不拥有也不删除任何 `Painter`，没人会通过基类指针删它，虚析构的表项就省下了。

## Widget 基类：一棵树两种走法

`include/visor/ui/widgets/widget.hpp` 里是基类的全部声明，实现在 `src/ui/widgets/widget.cpp`。每个控件三样东西：一块边界矩形、一个可见开关、一张孩子链表。控件的家园按类分目录：交互控件一组源码住 `ui/widgets/` 下的自己的子目录（按钮在 `ui/widgets/button/`），非交互的展示物归 `ui/primitives/`（矩形在 `ui/primitives/rectangle/`），painter、run_loop 这些公共件留 `ui/` 根上，它们不是控件，是控件脚下的地板。

孩子表是一条双向链表。每个控件记着头一个孩子、末一个孩子、前一个兄弟、后一个兄弟，孩子仍由使用者构造和持有，树只把它们的地址挂成一串，不加分配，也没有数量上限。`add_child` 拒绝三件事：空指针、已经有父控件的控件、以及会把控件挂成自己祖先的调用，成环的树递归走不完。调用方自己看着办，核心不终止。

末一个孩子和前一个兄弟这两个指针是给命中测试留的。绘制从头往后画，命中要从后往前问，单向链表反着走不动。

同一棵树有两种走法，方向相反。

绘制走先序：先画父节点，再按加入顺序画孩子，后画的盖在前画的上面。`draw` 里唯一的分支是可见开关，关掉的控件整棵子树都不进：

```cpp
void Widget::draw(const Painter& painter) const noexcept {
    if (!visible_) {
        return;
    }
    on_draw(painter);
    for (const Widget* child = first_child_; child != nullptr; child = child->next_sibling_) {
        child->draw(painter);
    }
}
```

命中测试走逆序：从最后加入的孩子问起，谁接住就归谁，谁都不接才轮到父节点自己。道理和绘制是同一条——后画的在上层，点击自然先落在上层。

```cpp
for (Widget* child = last_child_; child != nullptr; child = child->prev_sibling_) {
    if (Widget* hit = child->hit_test(at); hit != nullptr) {
        return hit;
    }
}
return this;
```

界面树的深度真实场景里就三四层（一块屏、一个面板、一个控件），递归在这里不构成吃栈的风险，换成显式栈反而要多发明一个深度上限常量，规格里没有这条。这个判断的理由写在源码注释里，评审的人看得见。

命中顺序的测试期望，我自己写错过一次。测试里 `left` 的边界和它唯一的孩子 `lefts_child` 完全重叠，我断言 `left.hit_test({5, 5})` 返回 `left` 自己，跑起来红在第一处：返回的是 `lefts_child`。想了一遍才反应过来，孩子是后加入的，画在 `left` 上面，命中就该是它。期望改成 `lefts_child`，绿。这个红留着没删，就是「后加者在上」的直接判例。

## 绘制可重放：条带化的前置承诺

`on_draw` 只有一条纪律：只画，别动状态。不读事件，不推进动画，不改任何成员。这条现在看着像洁癖，其实是条带渲染的前提——屏幕分成几条水平条带分别重建时，同一个控件会在一帧里被画好几次，只有画与状态分离，每一次才画得一样。

承诺落在测试里。测试造一个面板，上面压一个按下状态的按钮，往两张空画布里各画一遍，然后逐字节比对：

```cpp
panel.draw(visor::ui::CanvasPainter{replay_first});
panel.draw(visor::ui::CanvasPainter{replay_second});
VISOR_CHECK(std::equal(replay_first.pixels().begin(), replay_first.pixels().end(),
                       replay_second.pixels().begin()));
```

画两遍必须一个字节都不差。哪天有控件的 `on_draw` 偷偷改了状态，这个测试当场红给你看。

## Rectangle 与 Button：第一个有状态的控件

`Rectangle` 是纯装饰：一块填色，没有状态，不接事件。填的颜色是公开成员，测试直接改它。

`Button` 是第一个有状态的控件，全部状态就是一个 `bool pressed_`。它的反馈写在 `on_draw` 里：

```cpp
void Button::on_draw(const Painter& painter) const noexcept {
    painter.fill(bounds(), pressed_ ? canvas::Color::Off : canvas::Color::On);
}
```

空闲亮着，按下熄掉，像素一变就是反馈的全部。测试对状态和像素分别断言：按下之后 `pressed()` 是真，画进真画布后按钮范围内读到 `Off`，范围外还是 `On`——变的地方对，不变的地方也对。

一个实现里的小偏差留档：按压接口不带坐标参数。原计划写的是 `on_pointer_down(base::PointI p)`，落码时发现坐标在分发器那里已经用完了——命中测试决定谁收到事件，按钮只管翻转自己。带着一个用不上的参数过静态检查，还得标 `[[maybe_unused]]`，不如一开始就不给。

## 事件分发：控件只认接口，分发器只认 Widget

事件从宿主表进来，`std::visit` 按事件词分发。每个事件词对应一个具名的投递函数 `deliver`，一个词一份策略，指针按下和抬起走命中测试，找到谁就调谁的虚接口：

```cpp
void deliver(Widget& root, const host::PointerDown& word) noexcept {
    if (Widget* hit = root.hit_test(word.pos); hit != nullptr) {
        hit->on_pointer_down();
    }
}
```

`std::visit` 那头只剩一行，泛型 lambda 把词原样转交，靠重载解析找到对应的 `deliver`：

```cpp
std::visit([&root](const auto& word) { deliver(root, word); }, event);
```

这个形状有一条编译期保险：泛型 lambda 会对变体的每个候选词各实例化一份函数体，某个词没有对应的 `deliver` 重载，实例化当场编译失败。早期版本把这组分支写成 `if constexpr` 链，链尾不接的词会静默落地，加一个事件词忘了补分支，没有任何工具报错，两个形状行为完全一致，差别全在加词那天兑现。

`Widget` 基类上的 `on_pointer_down` 和 `on_pointer_up` 是虚函数，默认实现是空函数体：基类控件不接输入，什么都不做就是诚实的默认。`Button` 把两个都覆写。分发器手里只有 `Widget*`，调用走虚表，全程不问具体类型，RTTI 一行没沾。

键盘和指针移动两个词这一版没人消费，空的 `deliver` 函数体把这件事写在明面上：KeyUp 等键盘激活的松开跟踪，PointerMove 等手势。分发器是同一个，事件词在，消费者后到。Tab 键在下一批进场，落点就是 `deliver` 的 KeyDown 重载。

一个设计事实值得看清楚：抬起事件也走命中测试。抬起落在按钮外面，命中的是别的控件或者谁都不是，按钮收不到抬起，也就不会弹起来。这是这一版的明确行为，不是疏忽。真实按键要的「按住时滑出去松手」语义，等手势批次在分发器上补，控件不用改。

## run_once：一轮循环，全帧简化

规格里运行循环的完整管线是：处理事件，固定本轮场景状态，收集合并脏区，逐条带重建像素，同步提交，下一轮。脏区和条带是后面两批的事，这一批把首尾两环连起来，中间全帧重画，正确性靠「画得全」保住。

```cpp
template <typename CanvasT>
[[nodiscard]] int run_once(const host::HostTable& host, Widget& root, CanvasT& frame) noexcept {
    int consumed = 0;
    host::Event event;
    while (host::try_poll_event(host, event)) {
        ++consumed;
        dispatch_event(root, event);
    }
    const base::RectI full{.pos = base::PointI{0, 0},
                           .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                               .h = static_cast<int>(CanvasT::kHeight)}};
    render_window(root, frame, full, host);
    return consumed;
}
```

（代码块随第十一章同步过：提交一行换成了 `render_window`，窗口盖满整幅时它与原来的 `submit_frame` 干的是同一件事。）

循环留给使用者：`run_once` 跑一轮就返回，返回值是这一轮消耗掉的事件数。核心不做死循环，什么时候停是外面的事。

两个模板又出现在这里：`run_once` 模板挂在画布类型上（它负责造 `CanvasPainter`，这是全仓库唯一同时摸到画布和控件树的地方），`submit_frame` 第五章起就是模板。分发函数 `dispatch_event` 不是模板，定义住在 `src/ui/run_loop.cpp`，头文件里只有声明，第八章说过的头文件卫生口径在这里继续执行。

`submit_frame` 的返回值被显式弃掉了，带注释。表里没有 flush 项时它返回 false，帧发不出去，但核心不终止——这与 `try_poll_event` 对缺失项读成 false 的口径是同一条：边界上的缺失安静降级，调用方在意就在装配表的时候查一次，不用每一轮查。

## 测试的几个回合

三个回合全部走离屏宿主的表，一根测试桩都不碰内部。

第一回合，空事件树碰上无关事件：喂进一个键按下、一个指针移动，跑一轮，返回 2，帧照常提交。没有消费者的控件树接住了不认识的事件，什么都没做，也没坏。

第二回合，按下降落在按钮上：面板压着按钮，按下事件落在按钮范围内，一轮跑完，`pressed()` 为真，宿主收到的帧里按钮位置读到 `Off`，面板其他位置读到 `On`。事件进、状态变、像素出，一个闭环全部经宿主表走完。

第三回合，抬起降落在按钮外：按下在 `{4, 4}`，抬起在 `{15, 15}`。跑完一轮按钮仍然是按下的，像素也没有翻回去。上面说过的设计事实，测试里守着。

开发过程里工具还拦了几手，都照实录：结构体初始化要求指名字段（`SizeI{20, 20}` 被要求写成 `SizeI{.w = 20, .h = 20}`），`misc-const-correctness` 要求测试替身笔声明成 const，`set_clear` 少传了底色参数被编译器点名，`submit_frame` 的返回值弃掉时被 `-Werror` 拦下才补的显式转换。每一手都在教同一件事：让工具记住约定，人脑记思路。

## 验收

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

九组测试全绿后，直接跑测试程序看控件树的判例：

```bash
./build/tests/visor.tests && echo ok
```

红绿实验做一次命中顺序的。打开 `src/ui/widgets/widget.cpp`，把 `hit_test` 里的逆序遍历改成先序，起头处把 `last_child_` 换成 `first_child_`，推进处把 `prev_sibling_` 换成 `next_sibling_`。重新构建再跑，`widget_tests` 里两处命中断言转红：`{5, 5}` 落在三个重叠控件上，逆序时后加者赢，先序时先加者先接住。看完改回来，绿。

想再看一条的话，把 `run_loop_tests` 第二回合里按下事件的坐标从 `{4, 4}` 挪到 `{15, 15}`（面板上、按钮外），按钮收不到按下，`pressed()` 断言转红。命中分发「命中的才消费」这件事，红一次比读十遍清楚。

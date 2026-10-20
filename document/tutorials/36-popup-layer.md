# 第三十六章 弹层挂载

第二十四章的下拉框打开列表时改自己的 `bounds`，往下把整张列表展开。这个做法在一张卡片里够用，把卡片当成一块窗口就不够了。这一章我们给浮起来的内容换一个父件，让它不属于任何窗口。

## 看得见，点不着

我们拿一张 300×160 的卡片来试。卡片在 (40, 40)，下沿落在 y=200。一个 180×23 的下拉框挂在卡片里，左上角 (60, 170)，关闭行落在 170 到 193。四个选项，行高 23。照第二十四章的做法打开它，列表从关闭行的顶边往下铺，四行落在 170 到 262。第二行 193 到 216 跨着卡边，第三、第四行整行在卡外。

四行像素都上了画布。我们按在第三行中间，坐标 (70, 227)，下拉框什么都没收到。

我们顺着 `hit_test` 的走法看一遍。根看自己含不含这个点，再倒着问子件。卡片这一层遇到框外的点，直接返回空，卡片里的下拉框根本没有被问到：

```cpp
Widget* Widget::hit_test(base::PointI at) noexcept {
    if (!visible_ || !bounds_.contains(at)) {
        return nullptr;
    }
    // Reverse walk: the child added last paints on top of its siblings, so it is
    // asked first. A child that accepts the point ends the search; a child that
    // declines hands the point back to the remaining siblings and finally here.
    for (Widget* child = last_child_; child != nullptr; child = child->prev_sibling_) {
        if (Widget* hit = child->hit_test(at); hit != nullptr) {
            return hit;
        }
    }
    return this;
}
```

我们再看绘制那一边，那里没有剪刀。`draw` 只跳过不可见的子树，不管子件把像素画到哪里。所以越界的行照常上画布，判据却把它们挡在门外。同一块框，画与点的待遇不一样。

长框路还有另一笔代价。开合要改自己的 `bounds`，旧新两框都得报，挨着的兄弟被盖住一片。这笔代价第二十四章已经讲过，它对付的是兄弟。我们要盖住的往往是窗口外面的一切，兄弟只是其中很小一份。窗口在树里也是一个控件，它的框就是它的世界，它的兄弟只有那么几个。

要让内容越过窗口，我们得给父件换一个不属于窗口的控件。

## 一个整屏的层

我们把 `PopupLayer` 做成 `Widget` 的子类，它自己不画像素，构造时隐藏：

```cpp
PopupLayer::PopupLayer(base::RectI bounds) noexcept : Widget{bounds} {
    set_visible(false);
}
```

我们让它的框铺满屏幕，因为内容之外的点得有地方落。点落在层自己的框里，命中才走得到层身上，收回那一步才有可能发生。这块框顺带也挡住了下面的东西。层自己的绘制是空的，所有像素由内容画：

```cpp
protected:
    void on_draw([[maybe_unused]] const Painter& painter) const noexcept override {}
```

它为什么挂在根上，不挂在窗口里？因为我们查命中是逐层往下问的。层的上一层父件只要比屏幕小，外面那一下在父件那里就被挡掉，层连被问到的机会都没有。根通常就是框住整块画布的那个控件。

你把它加到根上，内容由 `open` 收养：

```cpp
bool PopupLayer::open(Widget* content) noexcept {
    if (content == nullptr || content == this || content_ != nullptr) {
        return false;
    }
    if (!add_child(content)) {
        return false;
    }
    content_ = content;
    set_visible(true);
    static_cast<void>(raise());
    return true;
}
```

我们在这里写三条拒绝，第四条交给 `add_child` 接住：已经有父件的内容加不进来。这个判据第三十三章补进树的时候就已经在那儿了，我们只是借它用一次。

收养成功以后，我们让它显示自己，再 `raise` 到根的子表末尾。`raise` 是这一步的全部意义。层排在最后一个，画在兄弟之后，命中时排在最前面被问到。

关上时我们走另一条路：

```cpp
Widget* PopupLayer::close() noexcept {
    if (content_ == nullptr) {
        return nullptr;
    }
    Widget* content = content_;
    if (!remove_child(content)) {
        return nullptr; // the layer stays open if the child cannot detach
    }
    content_ = nullptr;
    set_visible(false);
    return content;
}
```

收的时候，`close()` 让 `remove_child` 把内容摘下来，再把指针交给调用方，然后隐藏层。我们始终不销毁内容，所有权一直在调用方手里。`remove_child` 清掉内容的父指针、摘下它持有的焦点、断掉它的脏区接线，所以这块内容还能被别处再收养一次。重复调用返回空，不报错。

## 外面那一下

我们把层铺满屏幕，内容之外的每一点就都落在层自己的框里。`hit_test` 倒着问子件，内容排在前面，内容里的控件再走自己的路。剩下的点回到层身上，于是外面的按压直接落进层的 `on_pointer_down`：

```cpp
void PopupLayer::on_pointer_down([[maybe_unused]] base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (content_ == nullptr) {
        return;
    }
    Widget* content = close();
    if (on_dismiss_ != nullptr) {
        on_dismiss_(*this, *content, dismiss_context_);
    }
}
```

层关掉以后，这一次按压也到此为止。`dispatch_event` 已经把按下投给了层，不再往背景转一遍，所以我们不用担心背景的按钮在菜单关掉的一瞬间跟着被按一次。

`content_` 已经为空时它直接返回，你读到这里会明白它是给「关了以后又收到一次按下」留的余量。

移动与滚轮走同一条路，我们一并看。按下没被拥有时，移动投给指针下面最上面的控件，那还是层。滚轮投给悬停的控件，也还是层。所以浮层开着的时候，背后的悬停与滚动是停住的。内容里的移动和滚轮照常送达，因为命中问的是内容。

焦点不跟着走。层没有覆写 `focusable()`，基类答假，路由只在可聚焦的命中控件上设焦点。我们收回菜单，键盘焦点还在原处。

## 收回的通知

我们给 `set_on_dismiss` 一个函数和一块上下文，函数收三样东西：

```cpp
using DismissHandler = void (*)(PopupLayer& self, Widget& content, void* context);
```

回调被叫到的时候，内容已经从层上摘下来，对象还活着，调用方顺手就能拿到指针。显式 `close()` 不触发回调，两件事分开：一次是用户点了外面，一次是代码自己想收。调用方在自己的逻辑里关列表，状态本来就在手上，不需要我们再通知一遍。

## 开关各报整屏

我们把层的框铺成整屏，于是 `set_visible` 按自己的框报欠画，`raise` 在可见时也报自己的框。一次 `open` 里两笔申报，都是同一块整屏，`DirtyRegions` 的重叠合并把它们并成一条。判例就咬这个数：开关各一条，每条都是根框。

```cpp
VISOR_CHECK(layer.open(&menu));
VISOR_CHECK_EQ(reports.count(), std::size_t{1});
VISOR_CHECK_EQ(reports.at(0), root_bounds);
```

关上时我们看到同一件事。`remove_child` 报内容自己的框，`set_visible(false)` 再报整屏，合并以后还是一条整屏。

这就是第一版的代价：每开一次或关一次菜单，都要重画一整屏。它不漏像素，也不便宜。想省下这些重画，得换个挂法，让 `InputRouter` 在分发以前看一眼按压落没落在内容框里，落在外面就收回浮层。这一道判断叫输入守卫，有了它，层不用铺满屏幕，也就不用报整屏。这一章我们照正确性走，把代价如实记在判例里。

## 判例

我们在 `tests/popup_tests.cpp` 里用一个 `Spy` 记收到的动词和画到的顺序，六组。

第一组我们验构造与叠放。层加在两个兄弟中间，`open` 之前不可见、不可命中。`open` 之后内容成为唯一子件，绘制顺序是背景、后来加入的兄弟、内容。判例给三件控件编了号：1 是背景，2 是菜单，3 是后加入的兄弟，所以期望序列是 1、3、2。

```cpp
const std::array<int, 3> expected{1, 3, 2};
```

我们把三处命中也一起验。内容里答内容。(35, 35) 那个点住在后加入的兄弟里，答的是层。(5, 5) 住在背景控件里，还是层。

第二组验外部按压。层开着的时候我们点背景，层关掉，内容摘下，内容一下都没收到，回调一次并拿到内容指针，焦点原地不动。同一个位置再点一次，走的是普通背景输入，回调还是那一次。

第三组，我们点在内容里，内容收到按下与抬起，层保持打开，回调零次。

第四组验移动与滚轮。我们在外面发一次移动，它停在层上，在内容里发一次，内容收到，两件都不关闭浮层。

第五组验拒绝路径。空指针、层自己、已经打开、已经被别的父件收养，四条我们都试一遍，都返回假。`close` 返回内容、可以重复调用、显式关闭不回调。

第六组验申报，就是上一节那两行，你跟代码对一眼就够。

## 这一版的范围

层只管挂载、遮挡和外面那一下收回，我们没让它碰别的。菜单该有哪些项、分隔线怎么排、Esc 要不要接、键盘在菜单里怎么走，都不在层里，由装进层里的那个控件自己决定。第三十七章装进去的是下拉框的列表，选项、高亮和选中就由它接过去。

内容得自己把 `bounds` 放在绝对画布坐标上，我们不替它排版。层的整屏框只负责命中与遮挡。

层本身不可聚焦，这是我们故意留的。你想在浮层里放可聚焦的控件，由内容自己决定，按普通按下路径它照样会拿到焦点。

层要比内容活得久，内容归调用方。关闭以前内容若被外面销毁，层的指针会悬空，你 `close()` 之后再销毁。

叠放只保证到 `open` 那一刻。层 `raise` 到末尾以后，别的兄弟再 `raise` 会盖过它，什么时候调 `raise` 归调用方，我们不管。

## 验收

我们跑一遍 CTest，一条命令：

```console
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
$ cmake --build build
$ ctest --test-dir build --output-on-failure
```

你看到的成功条件是汇总那行以 `100% tests passed` 开头，退出码 0。popup 那六组判例的断言都编在 `visor.tests` 里，红一条这一行就不过。

接着我们把整串验收跑一遍：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

六道检查我们一道一道过。普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍代码，fontgen 的独立构建与测试收尾。CTest 现在挂着三个测试，`visor.tests`、`visor.signal.selftest`、`visor.snake.selftest`。末行打印 `acceptance: all green` 才算过，中间任何一道停下都不算。

红绿实验两个，你可以自己做。把 `on_pointer_down` 的函数体注掉，外部按压那组判例就红：层还开着，内容还在子件里，回调零次。复原再绿。再把 `open` 里 `raise()` 那行注掉，绘制顺序从 1、3、2 变成 1、2、3，菜单挪到了兄弟前面，命中 (35, 35) 也改答那个兄弟，绘制顺序与命中两处断言一起红。复原再绿。

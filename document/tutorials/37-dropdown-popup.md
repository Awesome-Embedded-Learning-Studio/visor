# 第三十七章 下拉列表上浮层

上一章我们做了一个铺满屏幕的层，这一章把下拉框打开时的列表搬上去，再给一个点得开、也能自己验的示例。列表从此不再住在窗口里，窗口的框也就管不到它了。

## 老路留在原处

`Dropdown` 的老路还在。没接层的控件打开列表时仍然改自己的 `bounds`，往下把整张列表展开，开合各报旧新两框。现有的控件判例、主题判例和 X11 窗口演示都走老路，我们这一章不动它们。

我们接上层以后，行为不一样：`Dropdown` 的 `bounds` 一次也不改，关闭行留在原处，列表作为一个独立控件画在整屏层上。两条路并存，因为它们服务的场景不同。列表不出卡片，老路够用，也不贵。列表要越过窗口，就接层。

我们把老路留着只为兼容，它不等于第二条等价的路。等所有使用方都接上层，我们才能删它。

## 把层接上

我们让 `set_popup` 只做一件事，把层记下来：

```cpp
void Dropdown::set_popup(PopupLayer* popup) noexcept {
    if (popup_ == popup) {
        return;
    }
    set_open(false);
    popup_ = popup;
}
```

换层以前，我们收掉当前的列表。这一步看着多余，其实必要：旧列表要是正挂在旧层上，关它一次才不会留下「控件以为关了、层上还挂着」的状态。

打开时我们把列表框算好，交给层：

```cpp
const int row_h = std::max(1, bounds().size.h);
highlighted_ = selected_ >= 0 ? selected_ : 0;
list_.set_bounds(base::RectI{
    .pos = base::PointI{bounds().pos.x, bounds().pos.y + bounds().size.h},
    .size =
        base::SizeI{
            .w = bounds().size.w,
            .h = row_h * static_cast<int>(count_),
        },
});
if (!popup_->open(&list_)) {
    highlighted_ = -1;
}
```

列表框放在关闭行的正下方，宽度跟着下拉框，高度是行高乘选项数。行高取 `bounds().size.h`，也就是关闭行自己那一行的高度，构造时定的。层拒绝的时候（比如它已经被别人占着），我们让高亮退回 -1，这个下拉框当作没打开，也不留一行高亮着、底下却没有列表的怪相。

`List` 我们做成 `Dropdown` 的私有嵌套控件：

```cpp
class List final : public Widget {
  public:
    explicit List(Dropdown& owner) noexcept;

    /// Reports the list's own pixels as owed; the enclosing dropdown
    /// cannot call the protected invalidate() on another widget.
    void refresh() noexcept { invalidate(bounds()); }

    void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    [[nodiscard]] int row_at(base::PointI at) const noexcept;
    Dropdown& owner_;
};
```

我们把它做成一个成员，不让它占堆：`List list_{*this}`，定容，跟选项表一样。它要读外层下拉框的选项表、字体、角色和主题，这几样都在 `Dropdown` 里，所以它揣着一份外层的引用。

`refresh()` 是我们给外面留的申报口。`invalidate` 住在保护区，只有自己的成员函数能调，`Dropdown` 替另一个控件报不了脏。`refresh` 报的是列表自己的框，正合需要。

## 开与关都去问层

接了层以后，`open_` 这个成员不再被写。状态住在层里，我们让 `open()` 换个地方问：

```cpp
bool Dropdown::open() const noexcept {
    if (popup_ != nullptr) {
        return popup_->content() == &list_;
    }
    return open_;
}
```

层的两个读取口你记一下。`content()` 返回当前挂着的那块内容，没挂内容时返回空。`is_open()` 回答层开没开，就是 `content_ != nullptr` 的另一种写法。

外面的点把层关掉以后，`popup_->content()` 是空，`open()` 自然答假，我们不需要谁去同步一份副本。这是接层最省心的地方。

关的时候我们也一样，只在层确实拿着自己的列表时才关层：

```cpp
if (popup_->content() == &list_) {
    static_cast<void>(popup_->close());
}
highlighted_ = -1;
```

你少写这个条件，一个下拉框关自己就会把别人挂着的浮层一起收掉。

## 列表自己收按压

我们让列表自己算行号，被点到以后把选中交给外层，然后一律关闭：

```cpp
void Dropdown::List::on_pointer_down(base::PointI at,
                                     [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (const int row = row_at(at); row >= 0) {
        owner_.select(row);
    }
    owner_.set_open(false);
}
```

行号落空的那一下我们也关，关掉不选。

我们把绘制也交给列表。边框和圆角用外层的主题，行用外层的 `draw_rows`，高亮那一行反色、其余普通，跟树内长框时的画法一致。反色看的是 `highlighted_`，它开列表时取 `selected_`，没有选中项才退到 0。示例的初始选中是第一项，画出来像第一行反色，其实反的是高亮行。

## 键盘仍归下拉框

列表不可聚焦，键盘仍旧回到 `Dropdown` 手里。我们让方向键在 `highlighted_` 上走，两头都停住：

```cpp
if (code == host::kKeyDown) {
    if (highlighted_ >= 0 && std::cmp_less(highlighted_ + 1, count_)) {
        ++highlighted_;
        invalidate_open_list();
    }
    return;
}
```

高亮变了，我们只让列表的框欠画：

```cpp
void Dropdown::invalidate_open_list() noexcept {
    if (popup_ != nullptr) {
        list_.refresh();
        return;
    }
    invalidate(bounds());
}
```

关闭行没变，重画它纯属浪费。老路里列表就在自己的 `bounds` 里，报自己的框正好。接了层以后列表是另一个控件，我们报它的框。

Enter 选中并关闭。空格有两个码，我们都要接，`kKeySpace` 是键盘宿主给的，`kCharSpace` 是文本宿主把空格当字符给出的那个。

## 一张卡、一个字段、一个层

`examples/popup` 是这一章的示例，我们照着读一遍。480×320 的画布，中间一张 300×160 的卡片，卡片在 (40, 40)，下沿落在 y=200。卡片里一个 180×23 的下拉框，左上角 (60, 170)，四个选项 `PAPER`、`NIGHT`、`OLED`、`X11`，行高 23，初始选中第一项。挂到层上以后，列表从关闭行的下沿 193 起，比树内长框时低了一整行。第一行跨着卡边，后三行整行落在卡外。根上放层：

```cpp
PopupDemo() : Widget{kScreenBounds} {
    static_cast<void>(add_child(&layer_));
    static_cast<void>(add_child(&panel_));
    static_cast<void>(panel_.add_child(&dropdown_));
    dropdown_.set_popup(&layer_);
}
```

顺序有讲究。我们把层在构造时加进根，比卡片早，所以平时它排在卡片前面，什么也不挡。开列表时 `open` 把层 `raise` 到末尾，列表这才画到卡片上面去。上一章那个 `raise` 在这里兑现。

我们让成员的声明顺序是 `layer_`、`panel_`、`dropdown_`，析构反过来走：下拉框比卡片早走，卡片比层早走。层要比接到它的控件活得久，靠的就是这个顺序。控件销毁以前得把列表关掉，`Dropdown` 的析构不碰层。

你把它跑起来，不带参数就走 X11：

```console
$ cmake --build build --target popup
$ DISPLAY=:0 ./build/examples/popup
```

窗口按两倍显示成 960×640。你点字段，列表出现在字段下方，越过卡片底边落在深色背景上。点一行，选中并收起。点别处，只收起，不改选中。关闭窗口点标题栏的 X。

## 无头自测与快照

`--self-test` 不需要显示服务，你直接跑：

```console
$ ./build/examples/popup --self-test
popup self-test: ok (outside dismiss, keyboard pick, pointer pick)
```

我们跟着自测走一遍。开列表以后，下拉框的 `bounds()` 与关闭时逐字段相同，而列表控件的框不是那个框。方向键按下一次把高亮移到第二项，Enter 选中并收起，`selected()` 是 1。在 (10, 300) 按一下，下拉框与层都关上。再开一次，点第三行，`selected()` 是 2，收尾时 `bounds()` 仍是关闭那一行。

第三行那个点的算法值得你看一眼：

```cpp
const int row_h = dropdown_.bounds().size.h;
const PointI second_row{
    70,
    dropdown_.bounds().pos.y + (3 * row_h) + (row_h / 2),
};
press(input, second_row);
```

局部名还叫 `second_row`，算式落在第三行：从关闭行的顶边往下量三行行高，再走半行，正好是第三行的中线。你按行高 23 算，落点 y 是 250。

CMake 里我们给它挂一个自测，命令就是 `popup --self-test`，测试名 `visor.popup.selftest`。

`--snapshot` 出图，你也不需要显示服务：

```console
$ ./build/examples/popup --snapshot build/popup-demo.pbm
```

它开着列表画整幅，写下 `P4` 和 `480 320`，再把每个字节取反写出去。画布上 1 是点亮的像素，也就是白墨，PBM 里 1 是黑。取反以后白墨落成 0 还是白，黑底落成 1 还是黑，出来的图跟屏幕上看到的一样，不是负片。480 是 8 的整数倍，一行正好 60 字节，没有补齐位，整字节取反就够。你直接打开 `build/popup-demo.pbm` 看，列表越过卡片下沿那一段应该落在背景上。

## 判例

我们在 `tests/choice_tests.cpp` 里加了一段，根、窗口、下拉框、层四件凑齐。判例把那个下拉框对象命名成 `list`，跟成员类 `List` 只差一个大小写，读代码时留意。窗口收养下拉框，根收养层，然后对下拉框对象调用 `set_popup(&layer)` 把两件接上。

我们在这一段里验六件事。`set_open(true)` 以后下拉框对象的 `open()` 与层的 `is_open()` 都真，而它的 `bounds()` 还是关闭那一行。层的列表落在关闭行下方，是 (4, 10) 起的 20×18，`root.hit_test(PointI{10, 12})` 答的也是它。画一遍，第一行亮、第二行暗。点在 (50, 50)，列表与层都关，`bounds` 不动。Down 加 Enter 选中第二项，回调一次。点第三行选中第三项，回调两次，`bounds` 仍然不动。

老路的三段判例（长框、像素、开合申报）我们一行没改，它们照样通过。

## 红绿实验

红绿实验两个，都值得你亲手做一遍。你把 `PopupLayer::on_pointer_down` 的函数体注掉，外面那一下就不再收浮层。`choice_tests` 里 `!list.open()` 与 `!layer.is_open()` 的断言一起红，`popup --self-test` 也会在外点那一段返回假，外点收回走的就是层这一侧。复原再绿。

你再把 `set_open` 的浮层分支换成老的 `set_bounds` 展开。列表还在树上，`bounds` 一变，`choice_tests` 里 `list.bounds() == closed` 的断言红。复原再绿。

## 验收

我们一条命令跑判例和示例自测：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

六道检查我们一道一道过。普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍代码，fontgen 的独立构建与测试收尾。CTest 现在挂着四个测试，`visor.tests`、`visor.signal.selftest`、`visor.snake.selftest`、`visor.popup.selftest`。成功条件是 ctest 那行以 `100% tests passed` 开头，与脚本末行的 `acceptance: all green`，退出码 0，中间任何一道停下都不算。

你也可以只跑这一件：

```console
$ cmake --build build --target popup
$ ./build/examples/popup --self-test
$ ./build/examples/popup --snapshot build/popup-demo.pbm
```

成功条件你逐一对：自测打印 `popup self-test: ok (outside dismiss, keyboard pick, pointer pick)`，快照文件头写着 `P4` 和 `480 320`，图是 480×320 的一位图。

最后你亲手点一遍。开 X11 窗口，点字段看列表越过卡片底边，点第三行看字段变成 `OLED`，再点字段开一次、点空白处收起，字段仍是 `OLED`。这三下都顺手，这一章就算过。

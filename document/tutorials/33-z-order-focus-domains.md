# 第三十三章 Z 序、焦点域与子件摘除

第三十二章把滚轮、修饰键和时间送进了事件表，控件树本身没有动。这一章我们改树：兄弟顺序第一次有了接口，焦点第一次有了边界，子件第一次能摘下来。三件都落在 `Widget` 与 `InputRouter` 上，绘制与命中的函数一行没改。

你打开 `include/visor/ui/widgets/widget.hpp`，看类声明里多了哪几个名字。

## 兄弟顺序就是叠放顺序

你读 `draw()`，它从父画到子，子按子表从前往后画。`hit_test()` 反过来，从后往前问。后画的盖住早画的，所以子表的顺序就是叠放顺序。

我们回头看这一章之前，控件树按父到子的次序画，命中从后往前问，焦点只有一个持有者，兄弟顺序却只能在 `add_child` 的时候定下来。想把一块面板临时提到最上面，没有接口。要改顺序，只能把子件摘下来再按新次序收养，而摘除本身也还不存在。

## parent_ 与 raise/lower

我们给控件添了一个 `parent_`。`add_child` 多了两处拒绝：空指针与已经属于某个父控件的控件，再加一条防成环的检查，不许把祖先挂成自己的孩子。

```cpp
bool Widget::add_child(Widget* child) noexcept {
    if (child == nullptr || child->parent_ != nullptr) {
        return false;
    }
    for (const Widget* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_) {
        if (ancestor == child) {
            return false; // A cycle no walk could finish.
        }
    }
    child->parent_ = this;
    child->prev_sibling_ = last_child_;
    child->next_sibling_ = nullptr;
    if (last_child_ != nullptr) {
        last_child_->next_sibling_ = child;
    } else {
        first_child_ = child;
    }
    last_child_ = child;
    ++child_count_;
    // The adopted subtree reports into the same sink and paints under the
    // same scheme, whenever either is wired.
    child->set_dirty_sink(dirty_);
    child->set_theme(theme_);
    return true;
}
```

我们让已经属于某个父控件的控件不能再被收养。否则同一张子表里会留下两份位置，`raise()` 与 `lower()` 该听谁的说不清。成环那条防的是走不完的树，递归转回自己身上就再也到不了叶子。

你收养新孩子时，它挂在链表末尾，画的时候盖住前面的兄弟，命中测试第一个问它。

你调 `raise()`，它把控件挪到父控件子表的末尾，`lower()` 挪到开头。两个名字共用一段函数：

```cpp
bool Widget::move_to_edge(bool to_top) noexcept {
    if (parent_ == nullptr) {
        return false;
    }
    Widget* parent = parent_;
    // Already at the asked-for end: nothing moves, so nothing owes repaint.
    if (to_top ? parent->last_child_ == this : parent->first_child_ == this) {
        return false;
    }
    // Close the gap first, so both ends of the list stay correct while this
    // widget is out of it.
    if (prev_sibling_ != nullptr) {
        prev_sibling_->next_sibling_ = next_sibling_;
    } else {
        parent->first_child_ = next_sibling_;
    }
    if (next_sibling_ != nullptr) {
        next_sibling_->prev_sibling_ = prev_sibling_;
    } else {
        parent->last_child_ = prev_sibling_;
    }
    if (to_top) {
        prev_sibling_ = parent->last_child_;
        next_sibling_ = nullptr;
        if (prev_sibling_ != nullptr) {
            prev_sibling_->next_sibling_ = this;
        } else {
            parent->first_child_ = this;
        }
        parent->last_child_ = this;
    } else {
        next_sibling_ = parent->first_child_;
        prev_sibling_ = nullptr;
        if (next_sibling_ != nullptr) {
            next_sibling_->prev_sibling_ = this;
        } else {
            parent->last_child_ = this;
        }
        parent->first_child_ = this;
    }
    // Only this box changes owner. The overlap the widget now covers (raised)
    // or uncovers (lowered) lies inside it, and every pixel outside it keeps
    // the same topmost painter, so one report is the whole debt. A hidden
    // widget owns no pixels; showing it later reports the box again.
    if (visible_) {
        invalidate(bounds_);
    }
    return true;
}
```

你读这段时先看开头的提前返回：已经在末尾的 `raise()` 与已经在开头的 `lower()` 都返回假，也不申报。中间是摘链和挂链两步，摘的时候把自己前后两位接上，挂的时候按要去的端点重新接。末尾的 `invalidate(bounds_)` 是重排成功后唯一一笔申报。

你可能要问，为什么只报自己的框。升到上面时新盖住的区域、降到底下时露出来的区域，都落在自己的框内。框外的像素前后由同一批控件按同一顺序画，谁也没换。不可见控件不申报，它当前不占像素，显示时 `set_visible` 会再报一次。没有父控件的控件，两个函数都返回假。

## remove_child

我们知道 `add_child` 原来只有进没有出。这一章补上配对的摘除接口：

```cpp
bool Widget::remove_child(Widget* child) noexcept {
    if (child == nullptr || child->parent_ != this) {
        return false;
    }
    if (child->prev_sibling_ != nullptr) {
        child->prev_sibling_->next_sibling_ = child->next_sibling_;
    } else {
        first_child_ = child->next_sibling_;
    }
    if (child->next_sibling_ != nullptr) {
        child->next_sibling_->prev_sibling_ = child->prev_sibling_;
    } else {
        last_child_ = child->prev_sibling_;
    }
    --child_count_;
    child->parent_ = nullptr;
    child->prev_sibling_ = nullptr;
    child->next_sibling_ = nullptr;
    // The pixels the child covered now belong to whatever is beneath it. A
    // hidden child painted nothing, so it owes nothing on the way out.
    if (child->visible_) {
        invalidate(child->bounds());
    }
    // A detached subtree cannot hold this tree's focus. Releasing the ring
    // before the sink is unwired also catches a holder that drew outside the
    // child's own box.
    release_focus_in(*child);
    // The subtree must stop reporting into a collector it is no longer part
    // of; adopting it again wires the sink the same way add_child does.
    child->set_dirty_sink(nullptr);
    return true;
}
```

我们让摘除把子件的前后邻居直接接上，计数减一，`parent_` 与两个兄弟指针清掉。可见子件按自己的 `bounds()` 申报欠画，因为它盖住的像素现在归下面的控件。不可见子件不申报，它当前不占像素。`set_visible` 相反，显示与隐藏都会报一次自己的框。

你接着读两行收尾。`release_focus_in(*child)` 清掉子树里所有焦点环，免得重新收养时带着旧环回来，全树出现两个持有者。`child->set_dirty_sink(nullptr)` 断开子树的脏区接线，免得它继续往一棵已经跟它无关的树里报欠画。

我们让对象本身不销毁。摘下来的控件归调用方持有，什么时候销毁也归调用方。重新 `add_child` 从末尾进来，回到最上层，sink 与主题照旧接上。

你拿空指针、不是直接子件、已经摘过一次去试，都返回 false，子表不动。

## 焦点域

你读旧的 `advance_focus`，它只认一棵树，从持有者往后走，走到底回绕。窗口一多，Tab 会从 A 窗口的最后一个控件走到 B 窗口的第一个。窗口有自己的边界，焦点不该随便跨过去。

我们给签名添了一个默认参数：

```cpp
bool advance_focus(Widget* scope = nullptr) noexcept;
```

你传空指针，或者传 `this`，域就是这棵子树，行为与旧实现相同。`scope` 是本树的子孙时，遍历只在那棵子树里走，到底回绕到域内第一个。指向树外或已经销毁的指针，退回根域。

我们让遍历在退回之前确认指针还落在活节点上。判例里有一个从没进过树的对象，局部名叫 `stranger`，它的地址合法，但不属于这棵树。`holds_widget` 只把地址和活节点逐个比，找到了才解引用。调用方漏清一个已经销毁的域指针，`advance_focus` 也会退回根域，不会去碰野指针。

```cpp
bool Widget::advance_focus(Widget* scope) noexcept {
    Widget* domain = (scope != nullptr && holds_widget(*this, scope)) ? scope : this;
    Widget* holder = find_focused_in(*this);
    Widget* current = find_focused_in(*domain);
    bool seen_current = (current == nullptr);
    Widget* target = find_focusable_after(*domain, current, seen_current);
    if (target == nullptr && current != nullptr) {
        seen_current = true;
        target = find_focusable_after(*domain, nullptr, seen_current);
    }
    if (target == nullptr) {
        return false;
    }
    if (holder != nullptr && holder != target) {
        holder->set_focused(false);
    }
    target->set_focused(true);
    return true;
}
```

你读这段容易漏一处。清旧焦点看的是 `holder`。`current` 只在域内找，域外的持有者它看不见。切换窗口时旧窗口的环如果不灭，整棵树就会同时亮两个环。

你让域里没有可见可聚焦控件，`target` 就为空，函数返回 false，焦点原地不动。即使当前持有者在域外，也不动。空域不抢焦点。

你隐藏一整条分支，遍历照旧跳过。`find_focusable_after` 一进门就看 `visible_`，整个隐藏窗口与窗口里的隐藏按钮都拿不到焦点。

## InputRouter 上的域

我们把域放在 `InputRouter` 上，不住控件树里：

```cpp
    void set_focus_scope(Widget* scope) noexcept { focus_scope_ = scope; }
    [[nodiscard]] Widget* focus_scope() const noexcept { return focus_scope_; }
```

你把这个指针当成调用方借给路由的地址。路由不拥有它，也不替调用方清。路由每次分发都把按下的归属与焦点域装进 `Delivery`，交给 `deliver`。`Delivery` 是 `src/ui/input_router.cpp` 里的投递参数包。Tab 走到 `advance_focus` 时，用的就是里面的 `focus_scope`。

```cpp
        std::visit(
            [&root, &input](const auto& word) {
                deliver(root,
                        Delivery{
                            .press_owner = input.press_owner_,
                            .focus_scope = input.focus_scope(),
                        },
                        word);
            },
            event);
```

你留空指针，域就是根域，全局 Tab 行为不变。切窗口就是再调一次 `set_focus_scope`，把域换成新窗口的子树。

窗口关闭时，你要按这个顺序走：`remove_child` 把窗口子树摘掉，然后销毁控件对象。摘除会清焦点、断脏区接线，域指针即使漏了清理，`advance_focus` 找不到活节点也会退回根域。摘除与销毁的次序反了，父表里已经留下悬空指针，回退只能救域，救不了父表。把域清空或换成下一个窗口，仍是调用方的责任，只有调用方知道窗口什么时候没了。

## 判例

`tests/widget_tests.cpp` 新增三组，我们逐组读下来，这一章的边界就齐了。

我们在 `run_z_order_cases` 里摆三个同框的控件，起手记下命中归最上面那个。`raise()` 中间那个之后，子表顺序变成底部、顶部、中间，绘制顺序跟着变，命中也换到中间那个。申报只有一笔，框是三个控件共用的 `left_bounds`。已经在顶端的再 `raise()` 返回假，申报计数为零。`lower()` 把它压到底部，顶部换回原来的控件。没有父控件的控件 `raise()` 与 `lower()` 都返回假。

我们在同一组还查了半开边界：右边界与下边界上的点归父控件，框外的点谁都不命中。隐藏的顶层控件不画也不命中，下一层可见控件接手。隐藏的控件仍然可以重排，但它当前不占像素，申报为零。

我们在 `run_focus_domain_cases` 里摆一个桌面、两个窗口、一个空窗口。窗口 A 里有按钮 `a1` 与 `a2`，窗口 B 里有 `b1` 与 `b2`。默认域是根，Tab 依次走过 `a1`、`a2`、`b1`、`b2`。给窗口 A 一个域，Tab 只在 A 里回绕。切到窗口 B 的域，A 的环灭掉，`b1` 接手，全树仍然只有一个持有者。清空域回到根，从 `b1` 走到 `b2`，再回绕到 `a1`。

你给空窗口一个域，它返回 false，焦点留在原地。隐藏 `a2`，A 的域里只剩 `a1` 可停。隐藏整个 B 窗口，B 的域返回 false，焦点不跑。拿一个树外的对象当域，遍历退回根域，那个对象从来没有拿到焦点。把域指成桌面自己，等同于根域。

我们在 `run_remove_child_cases` 里摘掉中间的子件，后面的兄弟前移，计数从三变二，`child()` 读出来是底部与顶部。申报一笔，框是被摘子件自己的框。被摘子件的 `raise()` 与 `lower()` 返回假，因为它已经没有父控件。绘制顺序与命中都跟着缩短的子表走。

我们在同一组还查了重新收养。被摘的控件再从末尾收进来，回到最上层，命中也落在它身上。空指针、树外的对象、第二次摘同一个子件，都返回 false，子表不动。隐藏的子件摘出去零申报。被摘的子树改了 `set_bounds`，不再往旧收集器报欠画。子树里持焦的控件在摘除时被清掉焦点，重新收养也不带回旧环。

你翻到 `tests/event_tests.cpp` 的末尾，另有一段走 `InputRouter`。窗口 A 的域里，Tab 依次落在 `a1`、`a2`，再回绕到 `a1`。切到窗口 B 的域，A 的环灭，`b1` 接手。清空域以后从 `b1` 走到 `b2`。树外的域退回根域，从 `b2` 回绕到 `a1`，树外那个对象从未获得焦点。

## 边界

我们把 `raise()` 与 `lower()` 做成到顶、到底，不是相邻一步交换。名字按 GUI 惯例取整段，相邻一步交换是另一个接口，这一章不做。

你读它的边界：`remove_child` 只摘直接子件，不销毁对象。对象归调用方持有，销毁时机也归调用方。

我们把按下的归属留在控件树外面。窗口关闭时如果正按住它，调用方得结束按压会话，否则 `InputRouter` 里的 `press_owner_` 会在对象销毁后悬空。

我们让摘除只申报被摘子件自己的 `bounds()`，前提是控件不画到自己的框外。`set_visible` 的申报范围也按自己的框算，区别在它显示与隐藏都会报，摘除只在子件可见时报。

你把域指针漏清，也只有按「摘除、销毁」的次序走才保得住。摘除与销毁的次序反了，父表本身已经坏了，退回根域救不了。

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

它跑普通构建、ASan/UBSan、TSan、clang-format、clang-tidy 与 fontgen 独立构建六道，任何一道失败就停。你看到最后一行是 `acceptance: all green`，就是六道全过。这一章的判例只走离屏画布与录制画笔，不需要显示服务。

两根红绿探针，你可以自己动手做。

第一根，你把 `move_to_edge` 末尾的 `invalidate(bounds_)` 注掉。重新构建后，`run_z_order_cases` 里「重排只申报一笔」的检查会红，`stack_reports.count()` 读出来是 0。复原再跑，退出码回到 0。

第二根，你把 `advance_focus` 里 `holds_widget` 的检查去掉，让 `domain` 直接取 `scope != nullptr ? scope : this`。重新构建后，树外那个对象会被当成域，Tab 停在原地不再前进。事件判例里 `a1.focused()` 的检查会红，焦点域判例里树外域的检查也跟着红。复原再跑，所有判例回到通过。

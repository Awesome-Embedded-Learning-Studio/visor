# 第二章 几何词汇表：Point、Size、Rect

第一章立好了骨架，这一章写第一批真正的代码。GUI 后面的一切对话都需要一套共同的词汇：画布说自己多大，笔说画到哪里，宿主说收到了哪块区域。我们把词汇表定在最底层，点、尺寸、矩形，外加它们赖以成立的类型约束。这一章结束时，`rect` 全部用例通过，半开区间的语义由一条试金石用例锁定。

命名空间从这章起开始分层：几何住在 `visor::base` 里，后面的位操作库、画布、笔、宿主各有自己的命名空间。

## 先定一条语义：矩形是半开的

写代码之前先拍板语义。一个 `Rect` 覆盖的区域是 `[x, x+w) × [y, y+h)`，数学上叫半开区间：左上角在界内，右边界和下边界在界外。宽或高小于等于零的矩形是空矩形，没有“负宽度翻转”之类的解释。

为什么第一步就定这个？因为边界的归属问题会渗进以后每一层。两个矩形恰好共享一条边，在半开语义下交为空，没有一条零宽的缝。以后做脏区域合并（计划中的后续批次）时，这条语义能省掉无数“边界算不算重叠”的争论。语义定了，剩下的就是让代码与测试一起检验它。

## Arithmetic：词汇表的准入条件

坐标可以是 `int`，也可以是 `float`、`double`。我们把“能做算术”定成一个 Concept（C++20 起的编译期类型约束：给模板参数提出条件，满足才允许通过），住在 `include/visor/base/concepts/arithmetic.hpp`：

```cpp
#pragma once

#include <concepts>
#include <type_traits>

namespace visor::base {
/**
 * @brief   Arithmetic means the type is a built-in arithmetic type, usable in coordinate math
 * @note    Bool is excluded: it satisfies std::is_arithmetic but has no meaning as a coordinate
 * @tparam  IsArithmetic the type to test
 */
template <typename IsArithmetic>
concept Arithmetic = std::is_arithmetic_v<IsArithmetic> && !std::same_as<IsArithmetic, bool>;

// The bool exclusion is a promise, so it is checked where the promise is made.
static_assert(!Arithmetic<bool>);
static_assert(Arithmetic<int> && Arithmetic<float> && Arithmetic<double>);

} // namespace visor::base
```

算术类型都放行，唯独 `bool` 被明确挡在外面。`bool` 在 C++ 里算算术类型，但 `Point<bool>` 没有合理的含义，在门口就说明白，不等它在某个模板报错里冒出来。挡 `bool` 这件事写在静态断言里，概念哪天被人改松了，编译期当场翻红，不用等哪支测试来逮。

这个文件名与 Concept 名都带着一段历史：收尾评审时它还叫 `Arithmetictable`，评审把改名挂成待办，首次提交之前改成了现在的 `Arithmetic`，连动文件名与全部引用。你跟着仓库走，看到的就是改完的样子。

## Point：先写测试，再写实现

我们按测试先行的节奏来。新建 `tests/point_tests.cpp`，登记进 `cases.hpp` 与 `main.cpp`，再新建 `tests/rect_tests.cpp` 给矩形用。这一批的测试我们先写，构建时 `include/visor/base/geometry/` 下的头文件还不存在，ctest 在编译这一步就红掉。这就是我们要的红：它证明测试确实在先。

### 一次关于指名初始化的课

写测试的路上 tidy 先上了一课。`Size` 与 `Rect` 的测试里到处是这样的写法：

```cpp
const RectI r{.pos = PointI{10, 20}, .size = SizeI{.w = 5, .h = 5}};
```

`.pos = ...` 这种写法叫指名初始化（designated initializers），C++20 起进标准，字段名直接写在初始化里。我们最初给 `Point` 也想这么写 `PointI{.x = 1, .y = 2}`，tidy 当场拦下。指名初始化只属于聚合体（aggregate：没有用户提供构造函数、无私有成员的朴素结构体），而 `Point` 带自己的构造函数，已经退出聚合体行列，只能按位置初始化 `PointI{1, 2}`。`Size` 和 `Rect` 全程没有构造函数，保持聚合身份，指名初始化一直合法。分界线就在这里：给类型加了构造函数，就同时放弃了指名初始化。

### Point 本体

`include/visor/base/geometry/point.hpp`：

```cpp
template <Arithmetic T>
struct Point {
    T x{};
    T y{};

    Point() = default;
    constexpr Point(T x, T y) : x(x), y(y) {}

    template <Arithmetic U>
        requires(!std::same_as<U, T>)
    constexpr explicit Point(const Point<U>& other)
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

    constexpr bool operator==(const Point&) const = default;

    constexpr Point& operator+=(const Point& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    constexpr Point& operator-=(const Point& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
```

这里贴的是节选，`*=`、`/=`、点积与全套自由运算符在仓库原文里。几处设计值得停下来看。

两个成员都带 `{}` 默认初始化，`Point() = default` 加上它意味着默认构造出的点是 `(0, 0)`，没有未初始化的悬空值。

跨类型转换构造函数标了 `explicit`，还用 `requires(!std::same_as<U, T>)` 排除了同类型的重复定义。`explicit` 在这里拦的是隐式窄化：`PointD d = somePointI;` 不会悄悄发生，要写就写明白 `PointD{somePointI}`，`int` 到 `double` 的加宽如此，将来反向的窄化更是如此。

`operator==` 用 `= default` 交给编译器，逐成员比较。这一章只要相等判断，不引入三路比较的义务。

### 测试里的括号课

`tests/point_tests.cpp` 开头一段：

```cpp
void run_point_tests() {
    using visor::base::PointD;
    using visor::base::PointI;

    // Same-type arithmetic, scalar mul/div, negation. Macro arguments holding brace
    // initialization need an extra pair of parentheses: the preprocessor balances
    // parentheses only, so commas inside braces would split the argument.
    const PointI a{1, 2};
    const PointI b{3, 4};
    VISOR_CHECK_EQ(a + b, (PointI{4, 6}));
    VISOR_CHECK_EQ(b - a, (PointI{2, 2}));
    VISOR_CHECK_EQ(-a, (PointI{-1, -2}));
    VISOR_CHECK_EQ(3 * a, (PointI{3, 6}));
    VISOR_CHECK_EQ(a * 3, (PointI{3, 6}));
    VISOR_CHECK_EQ(a / 1, a);
```

`PointI{4, 6}` 外面那层括号有实际作用，它服的是预处理器的规则：宏按逗号切分参数，也只认括号的配对，花括号在它眼里不算数，`PointI{4, 6}` 不套外括号就会被切成两个参数。断言宏的参数里只要出现花括号初始化，就得多包一层。这条注释就留在测试文件里，给以后每一个读它的人。

测试的后半段覆盖复合赋值、点积、`int` 到 `double` 的显式加宽：

```cpp
    // Dot product, plus the explicit converting constructor: int coordinates widen to double.
    VISOR_CHECK_EQ((PointD{1.5, 2.5}.dot(PointD{2.0, 4.0})), 13.0);
    VISOR_CHECK_EQ((PointD{PointI{1, 2}}), (PointD{1.0, 2.0}));
```

## 让 Point 能被打印

测试失败时要打印坐标，逐字段手写太啰嗦。`include/visor/base/debugging/point_format.hpp` 给 `Point` 特化了 `std::formatter`（标准库格式化的扩展点：特化之后 `std::print` 与 `std::format` 就认得这个类型）：

```cpp
/// Formatting support for visor::base::Point: std::print("{}", p) and std::format("{}", p)
/// render "(x, y)". Format specifiers are not supported and are silently ignored. The usual
/// route is to throw format_error from parse() on a non-empty spec, but this project bans
/// exceptions. The standard explicitly allows specializing std::formatter for user types,
/// the two namespace checks below do not recognize this extension point.
// NOLINTBEGIN(bugprone-std-namespace-modification,cert-dcl58-cpp)
template <visor::base::Arithmetic T>
struct std::formatter<visor::base::Point<T>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    auto format(const visor::base::Point<T>& p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};
// NOLINTEND(bugprone-std-namespace-modification,cert-dcl58-cpp)
```

注释把取舍写明：格式说明符不支持，标准做法是在 `parse()` 里对不认识的说明符抛 `format_error`，本项目禁异常，只能选择静默忽略，代价写在这里。

## Size 与“右下角”的算术

`include/visor/base/geometry/size.hpp` 短得能整贴：

```cpp
#pragma once
#include "visor/base/concepts/arithmetic.hpp"

namespace visor::base {
template <Arithmetic T>
struct Size {
    T w{};
    T h{};
    constexpr bool operator==(const Size&) const = default;
};

using SizeI = Size<int>;

} // namespace visor::base
```

没有构造函数，聚合体，所以测试里全是 `.w = ...` 的指名初始化。

`include/visor/base/helpers/point_helper.hpp` 给点与尺寸之间搭了算术桥：

```cpp
// The exclusive bottom-right corner reads as plain arithmetic: pos + size.
template <Arithmetic T>
[[nodiscard]] constexpr Point<T> operator+(const Point<T>& p, const Size<T>& s) {
    return offset(p, s);
}
```

`pos + size` 读作矩形的右下角，就是半开区间里第一个不属于这个矩形的点。这条算术让“贴着右下角放东西”这类代码读起来像数学。没有它，同一件事要写成 `PointI{r.pos.x + r.size.w, r.pos.y + r.size.h}`，一长串字段访问。

## Rect：语义的载体

`include/visor/base/geometry/rect.hpp`：

```cpp
/**
 * @brief Axis-aligned rectangle on a half-open area: it covers [x, x+w) x [y, y+h).
 *        A width or height of zero or less means an empty rectangle.
 */
template <Arithmetic T>
struct Rect {
    Point<T> pos{};
    Size<T> size{};

    constexpr bool operator==(const Rect&) const = default;

    [[nodiscard]] constexpr bool is_empty() const { return size.w <= 0 || size.h <= 0; }
    [[nodiscard]] constexpr bool contains(const Point<T>& p) const {
        return p.x >= pos.x && p.x < pos.x + size.w && p.y >= pos.y && p.y < pos.y + size.h;
    }
};

using RectI = Rect<int>;
using RectF = Rect<float>;
using RectD = Rect<double>;
```

`contains` 的四个比较就是半开语义的全部：`>=` 左与上，`<` 右与下。`is_empty` 对宽高做小于等于零的判断，负尺寸不需要特殊分支，它就是空。

`RectF` 与 `RectD` 这两行带了一段插曲：首评时它们误指向了 `Point`，是复制粘贴留下的错，评审判定后挂成待办，首次提交之前改成了现在的 `Rect<float>` 与 `Rect<double>`。跟跑教程的诚实义务不变：仓库当时什么样就写什么样，错过就交代，改了就说清改在哪一步。

求交运算放在 `include/visor/base/helpers/rect_helper.hpp`，不做成成员函数：

```cpp
template <Arithmetic T>
[[nodiscard]] constexpr Rect<T> intersect(const Rect<T>& a, const Rect<T>& b) {
    const T left = std::max(a.pos.x, b.pos.x);
    const T top = std::max(a.pos.y, b.pos.y);
    const T right = std::min(a.pos.x + a.size.w, b.pos.x + b.size.w);
    const T bottom = std::min(a.pos.y + a.size.h, b.pos.y + b.size.h);
    return {Point<T>{left, top}, Size<T>{std::max(T{}, right - left), std::max(T{}, bottom - top)}};
}
```

四条边各自取“更靠内”的那条，宽高再用 `std::max(T{}, ...)` 钳到非负。两个矩形不相交时 `right - left` 会算出负数，钳零之后自然得到空矩形，不需要不相交的特殊分支。

## 试金石用例

`tests/rect_tests.cpp` 把语义逐条检验。空矩形的部分：

```cpp
    // Emptiness: a default rect and any w/h <= 0 mean empty, no sign flipping.
    const RectI unit{.pos = PointI{0, 0}, .size = SizeI{.w = 1, .h = 1}};
    VISOR_CHECK(RectI{}.is_empty());
    VISOR_CHECK((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 4}}.is_empty()));
    VISOR_CHECK((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = -1}}.is_empty()));
    VISOR_CHECK(!unit.is_empty());
```

包含判定的边界，`r` 覆盖 `x` 属于 `[10, 15)`、`y` 属于 `[20, 25)`：

```cpp
    // Half-open containment: [x, x+w) x [y, y+h). The right and bottom edges are outside.
    // r covers x in [10, 15) and y in [20, 25).
    const RectI r{.pos = PointI{10, 20}, .size = SizeI{.w = 5, .h = 5}};
    VISOR_CHECK(r.contains((PointI{10, 20})));    // top-left corner is inside
    VISOR_CHECK(r.contains((PointI{14, 24})));    // last covered pixel
    VISOR_CHECK(!r.contains((PointI{15, 20})));   // right edge is outside
    VISOR_CHECK(!r.contains((PointI{10, 25})));   // bottom edge is outside
```

`(14, 24)` 是最后一个在界内的像素，`(15, 20)` 与 `(10, 25)` 分别踩在右边界和下边界上，都被拒在外面。空矩形连自己的原点都不包含：

```cpp
    // An empty rect contains nothing, not even its own origin.
    const RectI empty{.pos = PointI{3, 3}, .size = SizeI{.w = 0, .h = 0}};
    VISOR_CHECK(!empty.contains((PointI{3, 3})));
```

然后是这一章真正的试金石。测试上文定义过 `a`，即原点在 `(0, 0)`、覆盖 `[0,10)×[0,10)` 的正方形。两个矩形恰好共享一条边，交必须为空：

```cpp
    // Touchstone of the half-open semantics: rects sharing exactly one edge intersect to
    // empty, no zero-width sliver.
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 5, .h = 5}})).is_empty());
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{0, 10}, .size = SizeI{.w = 5, .h = 5}})).is_empty());
```

试金石的意思是：任何一条实现改动只要违背了半开语义，这条用例立刻红。零宽缝隙在半开语义里没有存身之处，一旦它出现，说明有人把某条边界从开改成了闭。

测试还覆盖了负坐标象限的求交、`pos + size` 的右下角算术，全部绿了之后，这一章的词汇表就可以交给后面各章使用了。

## 验收

跑整份测试：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`visor.tests` 通过，`point` 与 `rect` 两组用例都在其中。

再做一次红绿实验，亲手摸一下试金石。编辑 `tests/rect_tests.cpp`，把共享右边界那条断言的期望反转：

```cpp
    VISOR_CHECK(
        !intersect(a, (RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 5, .h = 5}})).is_empty());
```

重新构建并跑测试，你会看到一行类似这样的输出：

```
FAIL tests/rect_tests.cpp:54 !intersect(a, (RectI{.pos = PointI{10, 0}, ...
```

退出码非零，CTest 报 `visor.tests` 失败。这就是断言框架在说话：文件、行号、表达式原文都在。把 `!` 去掉，再跑一次回到绿。以后每一章出问题，你都会与这行输出打交道。

下一章往字节里走：位操作库。画布的每一比特都要靠它安放，而它本身带着一段从 Cinux 移植的故事，还修掉了两个真问题。

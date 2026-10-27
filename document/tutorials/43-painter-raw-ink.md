# 第四十三章 画笔打磨与裸墨通道

第四十一章收尾时咱们把下一站留给真 SPI 彩屏的纯逻辑半边，驱动宿主的命令流翻译加探针判据。走到彩屏之前，画笔层落了两批活，中间宿主一侧也往前迈了几步，宿主表长出生子程序的新口，环境预算跟着配了守门判据，第四十四章专门讲，咱们不在画笔章里展开。两批合起来讲一件事：画笔层长出绕过角色表的直达口，为逐笔彩色字铺底，角色主路原样。前一批是打磨，`Painter` 长出第五个动词 `draw_image`，把一整块像素当数据贴上来。后一批开渠，四个颜色动词各长出一个 `Ink` 重载，颜色值可以从数据里直来，不必为主题发明槽位。咱们按落地的顺序走。

## 第五个动词：像素当数据贴上来

咱们翻开 `include/visor/ui/painter.hpp`，`draw_image` 排在 `draw_bitmap` 与 `fill_rounded` 中间。头文件注释把它的身份说死，content imagery, not a role，贴的是内容图像，不是语义槽位。槽位咱们垫半句白话，说的就是控件报色时从中取名的七个 ColorRole，控件报名字、主题定颜色，裸墨一节咱们正式展开。咱们把参数逐个念下来。`words` 指向一整块 XRGB8888 像素，每个 32 位字按 0xAARRGGBB 读，高八位是透明度旗标，XRGB 格式把它读作忽略。`width` 与 `height` 是图像自己的尺寸，`stride_words` 是行距，按字数给，因为解码出来的图像常带着自己的行距，行尾可能有衬垫。注释末句把来路说清，this is the verb a windowed program's pixels arrive on，一个窗口程序的像素就从它到达画笔：

```cpp
    /// Pastes a rectangle of XRGB8888 words (0xAARRGGBB values, stride_words
    /// apart) at the given point: content imagery, not a role. Each source
    /// word is its own ink, and the target's traits fold it -- a 565 canvas
    /// quantizes the channels, a mono canvas takes the polarity (any
    /// non-black word reads lit). Parts outside the frame clip silently.
    /// This is the verb a windowed program's pixels arrive on: the words are
    /// data handed over, never state shared with the painter.
    virtual void draw_image(base::PointI at, const std::uint32_t* words, int width, int height,
                            std::size_t stride_words) const noexcept = 0;
```

末句的后半咱们单独看一眼。字是交过来的数据，不是与画笔共享的状态，画笔借指针用一阵，函数返回就还，一个成员都不为图像添。绘制要可重放的架构线因此不受牵连，条带重放每轮把指针再递一次，画笔自己不持任何图像状态。

实现住在 `CanvasPainter`，咱们看它进门查什么。空指针与不正经的尺寸进门就退，图像矩形与画笔自己的窗口求交，行走在交出的可见部分里：

```cpp
        const base::RectI visible = base::intersect(image, window_);
        for (int row = visible.pos.y; row < visible.pos.y + visible.size.h; ++row) {
            const std::uint32_t* source =
                words + ((static_cast<std::size_t>(row - at.y) * stride_words) +
                         static_cast<std::size_t>(visible.pos.x - at.x));
```

咱们顺着循环往下走：源指针按行距跳行，每个字经目标画布的 traits 折算成自家像素再写进画布。traits 是每种像素格式自带的折算规则，第三十八章认识过。写进画布用的坐标是窗口内坐标，场景坐标减窗口原点，与其它动词同一条换算。注释把条带重放的份额也说明白，the rows walk inside the painter's own window, so a strip replay paints exactly its share of the image。窗口外的字一个也不落地，画布外的同样不落。

## 判例：窗口半边与折算半边

`render_tests` 添的 66 行分两段，一段管窗口，一段管折算。咱们看窗口半段的布景。20x20 的画布，画笔窗口 10x10 落在 (5,5)，6x6 的图像贴在 (12,12)，与窗口相交的只有左上 3x3：

```cpp
        constexpr std::array<std::uint32_t, 36> black_words{};
        image_painter.draw_image(PointI{12, 12}, black_words.data(), 6, 6, 6);
        VISOR_CHECK_EQ(image_buffer.get(PointI{7, 7}), false); // scene (12, 12), black word
```

咱们把贴了两遍的判例对照着读。全黑的字贴过去，画布 (7,7) 对应场景 (12,12)，读回不亮。换成清一色 0xffc00000 的红字再贴一遍，(7,7) 与 (9,9) 亮，(10,10) 在窗外，不亮。判例里黑与红各走一次，窗口换算与单色画布的极性规则咱们一次核完，任何非黑的字读作亮。

折算半段咱们看它挑的布景：一张 2x2 的小图，四个字红、黑、白、红，让三档格式各折各的：

```cpp
        constexpr std::uint32_t red_word = 0xffc00000;
        constexpr std::array<std::uint32_t, 4> picture{
            red_word,
            0xff000000,
            0xffffffff,
            red_word,
        };
```

咱们把三档读回收进来看。565 画布的判据直接拿 `Rgb565Traits::from_ink` 对表，红字量化进 565 的通道、黑字折零、白字满格，读回值与折算函数的返回逐位相等。XRGB 画布红字原样 0xffc00000 落进去，黑字的透明度字节被强制成 0xff000000，格式名里的 X 说的就是忽略 alpha。单色画布上红亮、黑不亮、白亮。注释把三档的分野说成一句，the color targets keep their channels, and the mono target keeps only the polarity。

接口是纯虚的，动词一长，咱们判例里的替身全得跟着长。control 与 widget 两处的 `RecordingPainter` 把 `draw_image` 记成 `ImageDrawn`，位置加宽高，popup 的 `NullPainter` 补一个空实现：

```cpp
    struct ImageDrawn {
        visor::base::PointI at;
        int w = 0;
        int h = 0;
    };
```

编译器替咱们把名单过一遍，漏掉任何一个替身，测试目标整建制编不过。新动词在替身手里只登记形状，像素级的是非仍由 render_tests 两段判例把守。

## 裸墨通道：数据色的直达口

咱们回忆角色主路。控件从不说颜色，只说槽位，七个 `ColorRole`，从 Background 到 Disabled。`Ink` 是带透明度旗标的 0xAARRGGBB 颜色值，画布层的老词汇，`black`、`white`、`from_rgb` 三个工厂在 `color.hpp` 里坐镇，`from_rgb` 把高八位补成不透明。主题替每个槽位选一个 Ink，`CanvasPainter` 在构造期把七项折成目标像素的预折表，画一笔查一次表：

```cpp
    void resolve(const Theme& theme) noexcept {
        for (std::size_t i = 0; i < canvas::kColorRoleCount; ++i) {
            const auto role = static_cast<canvas::ColorRole>(i);
            const canvas::Ink ink = theme.ink(role);
            pixels_[i] = CanvasT::Traits::from_ink(ink);
        }
    }
```

咱们在 signal 与 snake 的示例里见过自定义主题，改的就是七项 Ink 的值。主路的好处是换主题不动控件。可有一类调用方主路接不住。终端模拟器要按 SGR 指令上色，SGR 是终端转义序列里管颜色的部分，256 色调色板按下标取色，颜色值是数据本身，不是语义槽位。解码一张图同理，像素值从文件里来。为每个数据色在主题里发明槽位行不通，256 色就要 256 个槽。头文件注释把立场写得很直：

```cpp
    /// The raw-ink lane, one overload beside every role verb: a color value
    /// straight from data -- a terminal's SGR palette, decoded imagery --
    /// rides these without inventing a theme slot for it. Targets fold the
    /// ink through their own traits exactly as draw_image's words already
    /// do: a mono canvas takes the polarity, a 565 canvas quantizes.
    virtual void fill(base::RectI area, canvas::Ink ink) const noexcept = 0;
```

咱们把四个新口子数下来：`fill`、`plot`、`draw_bitmap`、`fill_rounded` 四个颜色动词各长一个 Ink 重载，一对一站在角色版旁边。裸墨版的折算纪律与 `draw_image` 的字一模一样，单色画布取极性，565 量化，都经目标画布自家的 traits。Ink 从此是画笔层的正式词汇，不再只是主题内部的数据。

## 笔画只住一份

重载一多，咱们最怕两份实现各改各的，剪裁口径走出两个版本。`CanvasPainter` 的做法是把笔画收进私有帮手，帮手吃的是折算完的目标像素：

```cpp
    void fill(base::RectI area, canvas::ColorRole role) const noexcept override {
        fill_with(area, pixel(role));
    }

    void fill(base::RectI area, canvas::Ink ink) const noexcept override {
        fill_with(area, CanvasT::Traits::from_ink(ink));
    }
```

角色版查构造期预折表 `pixel(role)`，裸墨版当场走 `from_ink`。两条公开通道各剩一行，剪裁、窗口换算与 Pen 的活只在帮手里住一份。帮手注释把话说明白，the clipping and Pen work lives exactly once。咱们把 fill_with 的身子看全：

```cpp
    void fill_with(base::RectI area, Pixel px) const noexcept {
        const base::RectI visible = base::intersect(area, window_);
        if (visible.is_empty()) {
            return;
        }
        const draw::Pen<CanvasT, Pixel> pen{canvas_, px};
        pen.fill(shift_to_buffer(visible));
    }
```

求交、空判、窗口平移、起 Pen，全套只此一份。笔画落在窗外就静默消失的立场写在帮手注释里，笔画算法永不查界，与第五章以来的口径一致。咱们把帮手数一遍：fill_with、plot_with、rounded_with、bitmap_with，四个动词各得一个。fill_rounded 逐行收 span 时调的是 fill_with，不绕回公开通道，公共通道里没有任何一份重复的剪裁逻辑。

## 文本走位：一对孪生共享同一条路

文本层同样的题目咱们看大一码。`draw_text` 与 `draw_text_clipped` 各长一个 Ink 孪生，摆位决策绝不能写两份，字距、裁剪、边界像素的逐点 fill，任何一份走样，两条路当场分家。`src/ui/text.cpp` 的解法是把走位抽成色无关的模板帮手：

```cpp
// The walk lives once per shape and stays color-agnostic: a ColorRole and
// an Ink both pass straight through to the painter's overloaded verbs, so
// the twin public functions below share every placement decision.
template <typename Color>
void text_walk(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, Color color, int scale) noexcept {
```

咱们看模板参数 `Color`：它把颜色原样递给画笔的重载动词，角色与裸墨在帮手眼里没有区别。四个公开函数咱们数下来，各剩一行转发：

```cpp
void draw_text(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, canvas::ColorRole role, int scale) noexcept {
    text_walk(painter, at, font, utf8, role, scale);
}

void draw_text(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, canvas::Ink ink, int scale) noexcept {
    text_walk(painter, at, font, utf8, ink, scale);
}
```

两对孪生共享全部摆位决策，咱们把分法念一遍。`clipped_walk` 同款，内容区裁剪、越界字形的逐像素 fill 全在帮手里，边界像素走 fill、内部走 blit 的分法也不分色。文本层从头到尾不碰画布，全靠画笔的动词落地，所以裸墨要通到文本，入口必须开在画笔层，开对了位置孪生才落得便宜。

## 判例：调色板字逐像素读回

`text_tests` 添的 45 行咱们看它挑的画布：16x8 的 XRGB8888，最能暴露问题，颜色读回是逐字的，没有极性折叠替它遮掩。SGR 调色板的红，`Ink::from_rgb(0xcd0000)`，就是判例请来的调用方：

```cpp
    const visor::canvas::Ink sgr_red = visor::canvas::Ink::from_rgb(0xcd0000);
    const auto red_pixel = ColorCanvas::Traits::from_ink(sgr_red);

    ink_painter.fill(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 4, .h = 4}}, sgr_red);
    VISOR_CHECK(color.get(PointI{11, 1}) == red_pixel);
    ink_painter.plot(PointI{15, 7}, sgr_red);
    VISOR_CHECK(color.get(PointI{15, 7}) == red_pixel);
```

fill 与 plot 各交一个字，咱们把读回的两种身份都认下：既是调色板红本身，也是 XRGB 画布存下的字面值。文本判例借 `kTypeFont` 的 h 字形说事，两位宽、四行高、两位全亮、步进三。画一个 h，(0,0) 与 (1,3) 该读回调色板字，步进留出的空隙 (2,0) 该读回底色。裁剪孪生把 clip 定成两位宽，正好咬掉 h 的右列：

```cpp
    visor::ui::draw_text_clipped(ink_painter,
                                 RectI{.pos = PointI{4, 0}, .size = SizeI{.w = 2, .h = 8}},
                                 PointI{4, 0}, kTypeFont, "h", sgr_red, 1);
    VISOR_CHECK(color.get(PointI{6, 0}) == ground_pixel); // past the clip's edge
```

咱们把两条路径读在一起。注释说明边界像素走 fill、内部走 blit，both arrive in the same ink，(4,0) 与 (5,3) 读红，(6,0) 出了裁剪边不红。逐点 fill 的路径与整块 blit 的路径落在同一抹墨里，孪生的「同走位」在像素级有了证人。末尾单色画布把同一抹红折成亮，极性规则与 `draw_image` 的字同一条。

替身一侧咱们照旧数过去。widget 的 `RecordingPainter` 把裸墨 fill 记进 InkFill 清单，连访问器都备好了，暂时还没有判例来消费，等逐笔彩色字的真消费者出现。control 与 popup 的替身同样补齐四个重载，谁缺席谁就让测试目标编不过。

## 两条路的关系：主路原样

咱们把两条路的关系说准。控件照旧只报角色，Label 报 Foreground，按下的按钮报 Accent，主题决定槽位长什么样，预折表构造期折一次，一笔一查。裸墨版是给数据色的旁路，入口开在画笔层，不开在控件层，控件代码一行未动。逐笔彩色字到来时的分工就清楚了：文本绘制按笔给墨，一个字一个 Ink，调用方从数据里取色，画笔经 traits 折成目标像素。控件该说话的地方照旧说角色，两不搀和。draw_bitmap 的墨转移语义两边同款，亮源像素变墨、暗源像素不画，擦底是调用方自己的 fill，裸墨版照走同一条，没有另一套要记。绘制无副作用的架构线也没让步，裸墨笔画一样只写像素，不读事件、不改场景状态，条带重放照旧安全。判例侧有现成的证据，裸墨通道落地时既有角色判例零变化，`render_tests` 未动一行，`text_tests` 的角色段只添未改，全部照旧绿。

## 红绿实验

两根探针咱们都亲手跑过核过。

第一根做在折算上。咱们把 `CanvasPainter` 裸墨版 fill 里的 `from_ink` 换成直接给零像素，Ink 参数弃置不用，构建跑 `visor.tests`。text_tests 两根探针当场红：

```console
FAIL tests/text_tests.cpp:302 color.get(PointI{11, 1}) == red_pixel
FAIL tests/text_tests.cpp:329 mono.get(PointI{2, 2})
```

咱们把两根红的意思读出来：XRGB 画布上 (11,1) 读回的不是调色板红，单色画布上 (2,2) 不亮。红得诚实，裸墨通道若不真折算，彩屏读不回颜色，单色屏丢极性，两条判据各守一头。改回去恢复绿。

第二根做在共享走位上。咱们把 `text_walk` 里摆位的横坐标加一，只动 text_walk，不动 clipped_walk。角色的判例与裸墨的判例一起红：font 判例 15 处，control 的 Label 判例 7 处，theme 判例 16 处，加上裸墨文本自己的两根：

```console
FAIL tests/control_tests.cpp:142 painter.bitmaps()[0].at == (PointI{2, 3})
FAIL tests/font_tests.cpp:214 canvas.get(PointI{4, 2}) == true
FAIL tests/text_tests.cpp:309 color.get(PointI{0, 0}) == red_pixel
```

走位偏一格，角色判例与裸墨判例一齐偏，Label 的笔画落点从 (2,3) 挪到 (3,3)，裸墨 h 的左列落进空隙，(0,0) 读回底色、(2,0) 反倒亮了。摆位决策只住一份，是咱们从红的面孔里直接读出的事。clipped_walk 没动，裁剪孪生的判例一处没红，两条走位互不寄生。改回去恢复绿。

## 验收

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件是汇总行以 `100% tests passed` 开头、退出码 0。咱们在干净检出上实跑，四个目标全过，`visor.tests` 耗时 135.15 秒。

验收脚本随画笔打磨的批次分了道，咱们把两道读清楚。默认 quick 道构建三个矩阵加两道静态检查，判例不跑，给迭代回路省时间，`--full` 才把判例全跑。收工口径照旧是全道绿：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh --full
lane: full (builds, checks and every judgment)
acceptance: all green (full lane)
```

咱们实跑六道全绿。rel 道测试 135.15 秒，ASan/UBSan 测试 134.90 秒，TSan 测试 135.32 秒，三道矩阵各四个目标全过，格式与 tidy 各扫一遍，fontgen 独立构建加两个判例 0.06 秒收尾。tidy 的跑法同一批也换了，单进程串行整份清单改成每个文件一个 clang-tidy 进程，按核数散开，清单经生成文件递给脚本，分钟级的串行等待变成并行。末行的 `acceptance: all green (full lane)` 就是成功条件，quick 道的末行会提醒你补跑 `--full`。

## 下一站的口子

口子咱们照实记。裸墨通道与 `draw_image` 的真消费者都还没落地。逐笔彩色字的头一个调用方，按待办的走向看是终端线，第四站的终端条目挂着它，SGR 调色板直连 Ink，文本层现成，等一个真的终端控件来接。示例仍停在 Mono1 与 RGB565 两档，逐笔彩色的演示要跟消费者一起来。第四十一章挂的彩屏纯逻辑半边继续挂着，驱动宿主的命令流翻译在后面的批。角色表没有欠着的事，七槽预折的表照旧服务全部控件。

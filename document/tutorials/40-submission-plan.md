# 第四十章 提交策略：区域攒成一批

第二十九章结尾咱们留过话：「之后是 X11 的提交策略（脏区矩形到 X 提交矩形的裁剪与合并）」。中间隔了像素与条带的长路，这一章把这句话兑现。核心交出的是一轮一轮的区域：条带重建一来就是五带，旧路上一带一次 put、一次跟着一次 flush。这一批在区域与 put 之间立了一个策略件 `SubmitPlan`，把一轮的区域攒成一批走光，条带重建从五次 flush 降到一次。同一批顺手把 cinux 帧展开的 RGB565 向补上，咱们在这章末尾看它一眼。

## 线上的老样子：一次 put 一次 flush

咱们把旧路翻出来看。`handle_flush` 收到一个区域，画进影子帧，然后当场提交：

```cpp
paint_shadow(region);
++flush_count_;
put_region(region.rect);
```

而那时候 `put_region` 的末尾自带一句 `XFlush`。XFlush 做的事咱们一句说清：把 Xlib 缓冲里的请求推上线路，XSync 在它之外还要等服务答完。所以第三十九章 `--strip` 在桌面跑出来的节奏是：五带五次 put，五次 flush，五趟把请求推上线路。像素一个不缺，缺的是线路喜欢的形状，put 本来就是想成批的请求。

咱们停下来把这笔代价数出来。一轮里核心交出的区域往往本来就属于同一次更新：条带重建的五带是一次全量重画的五个窗口，脏区清理后的两三块矩形是一次状态变更的碎片。逐区 flush 等于把同一句话拆成五遍说。要攒批，有两个问题要回答：哪些矩形算一批，攒下的一批什么时候走。这一批给第一个问题的答案是 `SubmitPlan`，给第二个问题的答案是下一轮 poll 开门的时候。两个答案，一个住在核心与宿主都能看见的 bindings 层，一个留在 X11 宿主自己手里，职责不混在一个类里。

## SubmitPlan：区域进计划的三道工序

咱们翻开 `include/visor/bindings/submit_plan.hpp`，整个类一百二十来行，词汇是纯几何，没有任何平台头。类注释把它站在哪里一句话说清：

```cpp
/**
 * @brief The submission policy between the regions a core submits and the
 *        puts a host sends: clip to the window, merge what touches, collapse
 *        when full.
 */
```

咱们看 `push`，三道工序都在里面：

```cpp
constexpr void push(base::RectI area) noexcept {
    const base::RectI visible = base::intersect(area, bounds_);
    if (visible.is_empty()) {
        return;
    }
    if (count_ == kCapacity) {
        collapse();
    }
    base::RectI grown = visible;
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (merges(areas_[i], grown)) {
                grown = base::bounding_box(areas_[i], grown);
                areas_[i] = areas_[count_ - 1];
                --count_;
                merged = true;
                break;
            }
        }
    }
    areas_[count_] = grown;
    ++count_;
}
```

咱们逐道看下来。第一道裁剪：入区与宿主自己的矩形求交，空了就丢，越界的部分从来不进计划。第二道合并：新矩形跟每个住户问一遍 `merges`，碰上就并成两者的包围盒，并完的盒子继续问，级联到没有邻居可碰为止。第三道坍缩：清单满了（容量八，跟脏区收集器同一个数）就把全部住户折成一盒腾出格子，这是收集器立过的溢出立场。三道工序都是 constexpr，编译期能跑完整个类。

批出去的口子咱们数下来是三个只读动词加一个退休动词。`count` 与 `at` 把计划矩形按不承诺的顺序摆出来，注释明说 Coverage of everything pushed is the contract, arrangement is not。`empty` 问有没有东西等着，`clear` 退休整批，被送空的宿主在 puts 出门之后调它。默认构造的计划持有空界，头注释给它的评语是 a host built but not yet opened drops instead of batching，开门之前的宿主只能丢，攒了也没有去处。

## 跟收集器比一比：贴边与角点

这套工序跟第十一章的脏区收集器几乎同款，咱们把合并谓词摆在一起看差异。收集器那边：

```cpp
if (!base::intersect(areas_[i], grown).is_empty()) {
```

收集器只在真实相交处合并。计划这边咱们看 `merges`：

```cpp
const bool x_overlap = a.pos.x < (b.pos.x + b.size.w) && b.pos.x < (a.pos.x + a.size.w);
const bool y_overlap = a.pos.y < (b.pos.y + b.size.h) && b.pos.y < (a.pos.y + a.size.h);
const bool x_touch = a.pos.x <= (b.pos.x + b.size.w) && b.pos.x <= (a.pos.x + a.size.w);
const bool y_touch = a.pos.y <= (b.pos.y + b.size.h) && b.pos.y <= (a.pos.y + a.size.h);
return (x_overlap && y_touch) || (x_touch && y_overlap);
```

咱们把两份谓词的意思摆开。一个轴上严格重叠、另一个轴上贴上或重叠，就并。半开区间的口径下，贴边意味着两块矩形首尾相接。差别的原因写在职责上。收集器的词是欠画：一个像素被碰过是事实，两块区域只在共享像素时才算同一件事，贴边不共享任何像素，并进去就把没欠过的像素也记成了欠画。计划的词是打批：五条首尾相叠的条带本来就是同一轮的内容，贴边并成一盒少发一次 put。角点接触两块只碰一个点，没有共享边，不并，头文件里把不并的立场写成了明文。

咱们再念覆盖的承诺。合并可能重画没人推过的像素，第三十九章念过的分界在这里原样成立：折叠是正确性，合并是节俭。多画的像素费字节不伤正确性，因为 put 读的是影子帧，影子帧已经持有这一轮的全对像素，画出去的每一笔都是对的。判例把这个承诺翻成一个能问的问题：`covers` 问一块覆盖矩形是否装得下另一块，`some_plan_covers` 问计划里有没有哪一格装得下，溢出坍缩那段后面咱们还会用到它们。

## X11 宿主接上计划

宿主这边咱们看三处改动。第一处在 `handle_flush`，画完影子帧不再当场 put：

```cpp
paint_shadow(region);
++flush_count_;
// The put is a batch, not a call: the rectangle joins the plan and the
// next poll's drain carries it out. The shadow already holds the pixels,
// so however late the put leaves, it sends the right ones.
plan_.push(region.rect);
```

咱们看第二处，在 `poll_one` 的入口，新添的 `drain_submissions` 把上一轮攒的批次一次送出：

```cpp
void X11WindowHost::drain_submissions() noexcept {
    for (std::size_t i = 0; i < plan_.count(); ++i) {
        put_region(plan_.at(i));
    }
    if (!plan_.empty()) {
        XFlush(display_);
    }
    plan_.clear();
}
```

第三处在 `put_region` 自己：末尾那句自带的 flush 撤了，注释写明用意，请求留在 Xlib 缓冲里等调用者的 flush，整批一起走。咱们把三处连起来读就是：逐矩形 `put_region`，整批跟一次 `XFlush`，然后清空计划。`open` 的时候窗口矩形装进计划当 bounds，宿主从此带着计划过活。

咱们数一遍桌面上的 flush。480x320 的示例屏配 64 行条带，一次全量重建五带。旧路五次 put 五次 flush，新路五带进计划折成一盒，下一轮 poll 开门时一次 put 一次 flush。一轮的区域一批走光，这就是这批的主线。头文件的类注释末尾把执行时机划给宿主：The plan holds no policy about when the batch executes，计划只管攒，什么时候走是宿主的事。

判例要看批次的行踪，咱们给宿主添了一个探针口子：

```cpp
/// Rectangles sitting in the submission plan, waiting for the poll that
/// drains them. A judgment's probe for the batching: five strips in, one
/// merged rectangle pending.
[[nodiscard]] int pending_submissions() const noexcept;
```

咱们把注释与判例对着数：注释举的例子是桌面那五带，批式判例用的是小屏的八带，数不同、形状同，几条带进来，一盒等着。

## 修复不等批：Expose 当场送

有一类区域不进计划。窗口管理器把窗口遮了再揭开，X 服务器送来 Expose，屏幕此刻缺像素。咱们看 `poll_one` 里的分支：

```cpp
const base::RectI exposed{
    .pos = base::PointI{raw.xexpose.x, raw.xexpose.y},
    .size = base::SizeI{.w = raw.xexpose.width, .h = raw.xexpose.height},
};
const base::RectI owned =
    base::intersect(exposed, base::RectI{
                                 .pos = base::PointI{0, 0},
                                 .size = base::SizeI{.w = width_, .h = height_},
                             });
if (!owned.is_empty()) {
    // A repair leaves at once, its own flush included: the
    // screen is missing pixels right now, batching would only
    // prolong the hole.
    put_region(owned);
    XFlush(display_);
}
```

暴露矩形裁到画布拥有的那一角，然后 `put_region` 加 `XFlush` 当场送走，注释里的理由咱们念过：屏幕正缺像素，攒批只会延长空洞。修复不是批。攒批换的是线路的形状，代价是像素晚一轮 poll 才走，正常更新晚这一拍没有代价，空洞晚这一拍屏幕上就多亮一拍窟窿。裁剪那一半也在：窗口管理器硬拉大窗口时暴露矩形可能越过画布角，尺寸提示只是劝告不是法律，只有画布那一角是咱们的，越界的部分影子帧里根本没有，不裁就写出了缓冲的边界。影子帧在这里干的就是它的本职，它是屏幕在咱们这一侧的内存替身，Expose 重放的是它，批式 put 读的也是同一份像素。

## 计划的判例

九段纯逻辑都在 `tests/submit_plan_tests.cpp` 里，咱们挨段看。

咱们从裁剪那段看起：一块 10 宽的矩形从 x=15 进 20 宽的窗口，留下 15 到 20 的 5 宽，一块完全在界外的丢掉，计数不动。三叠带那段是条带重建自己的形状：0 到 8、8 到 16、16 到 20 三块贴边相叠，折成整屏一盒。并排那段左右两半严格共享一条竖边，合成一盒不多一个像素，因为并集就是包围盒。隔行与角点那段守不合并的边界：隔一行的两块各自在册，跟对角邻居只碰一个角点的一像素矩形也不并，三块三格。L 级联那段三块折尺形逐次长大，最后一盒 14x10 把三块全收进去。

溢出坍缩那段咱们多看两眼。九个互不相交的一像素推进容量八的清单，前八折一盒第九自占一格，然后判例把覆盖当成问题来问：九块每一块都得住在某个计划矩形里，盒半边 0 到 15 与 newcomer 也各有人罩。clear 那段退休动词清空后同界再推，新批从零开始。预开门那段默认构造的计划持有空界，什么进来都裁成空，开门之前的宿主丢而不是攒。最后一段编译期跑，三叠带的推进折一整个搬进 lambda，`static_assert` 在编译期把结果写成事实，谁改坏了合并谓词不用等测试，构建当场停。

## 批式判例与读回的新节奏

纯逻辑之外咱们再看 `tests/x11_host_tests.cpp`，它自己起 Xvfb 走真服务器。批式判例摆一个 128x64 的屏，8 行一带，八带循环送进宿主：

```cpp
for (int top = 0; top < kHeight; top += 8) {
    const visor::base::RectI band{.pos = PointI{0, top}, .size = {.w = kWidth, .h = 8}};
    const std::uint8_t* band_pixels =
        frame.pixels().data() + (static_cast<std::size_t>(top) * Frame::kStrideBytes);
    VISOR_CHECK(host->table().submit_region(band, band_pixels, Frame::kStrideBytes));
}
VISOR_CHECK_EQ(host->flush_count(), 8);
VISOR_CHECK_EQ(host->pending_submissions(), 1); // eight bands, one box
visor::host::Event event;
VISOR_CHECK(!host->table().poll(event));
VISOR_CHECK_EQ(host->pending_submissions(), 0);
XSync(screen.display(), False);
compare_readback(screen.display(), host->window(), frame);
```

咱们按行数下来。画面是黑底上两块亮带，0 到 8 行与 16 到 24 行，中间夹一条暗带，读回能不能逐点对，暗带与两块亮带都在作证。八次 submit，`flush_count` 是八，影子帧被画了八回。`pending_submissions` 是一：八带在计划里折成一盒。poll 一次，pending 归零，批次跟着一次 flush 一起出门。`XSync` 等服务器答完，读回 8192 个窗口像素逐点与画布对。第三十九章留过话「X11 宿主收条带区域没有像素级读回的判例」，这一角补上的是单色，桌面示例的五带全量重建是同一个形状的放大版。

咱们再看另外两段读回判例，它们跟着改了节奏。put 现在走下一轮 poll，读回之前得问宿主要一轮，判例里 submit 之后那句 `poll` 就是这个意思，注释写着 the drain rides the poll。整帧落地那段与奇 x 区域那段都是这个新节奏：poll 一次让批次出门，`XSync` 等服务器落笔，再逐像素读回。奇 x 那段还在守老承诺，x=5 起步的区域字节不对齐两头，区域读回新值、邻位保持旧值、字节边缘的邻居不动。这个节奏在真程序里免费成立：主循环每轮都 poll，上一轮的批次跟着这一轮的开门走掉。

## 同一批的另一半：cinux 补 565 向

咱们把这一批的另一半也看完，它落在 cinux 绑定上。帧展开原来只有单色向，把 Mono1 的位展开成帧缓冲的 XRGB8888 字，这一批补上换向的 RGB565 半边：核心用 565 渲染时，`expand_region_rgb565` 逐像素过第三十八章判过的 `rgb565_to_xrgb8888`，落成 32 位字。判例用黑、白、鸢尾蓝、蓝灰四色各占一像素的条验证，鸢尾蓝 0x4a62c8 窄到 565 再宽回来是 0xff4a61ce，绿从 0x62 落 0x61、蓝从 0xc8 变 0xce，两个字节上都现形，有损窄向就在这几位里。下一章的交叉探针会把 565 向的展开也喂给别的编译器。

## 红绿实验

这些实验咱们都在干净检出的代码上跑过一遍，两红一绿，绿的那个也有它要说的事。

咱们把第一个做在合并谓词上。把 `merges` 末行改成 `return x_overlap && y_overlap;`，也就是只认严格重叠的收集器立场，重新构建跑测试。三叠带那段当场红：三块贴边矩形谁也不严格重叠谁，`plan.count()` 数出来是三，判例要一。并排那段同样红，左右两半只有一条共享边。改回去恢复绿。

第二个咱们从不红的试起，再看一个红的。把 `drain_submissions` 里的 `XFlush` 抽走，构建跑测试，判例全绿，原因在每段读回判例自己：比较前都有一句 `XSync`，XSync 会把 Xlib 缓冲里的请求全部推出，像素照样到位。这暴露一个盲区：单次 flush 是这批的承诺，却没有判例验它，节奏本身没有验收。咱们换成把 `poll_one` 入口的 `drain_submissions()` 注掉，这回红得干脆：批式判例的 `pending_submissions() == 0` 第一个红，批根本没有离开，接着读回比较红了 6164 处，窗口里一个像素都没到，XSync 救不回从未发出的请求。改回去恢复绿。

## 验收

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件是汇总行以 `100% tests passed` 开头、退出码 0。计划的九段与批式判例都编在 `visor.tests` 里，X11 判例自己拉 Xvfb，整串跑两三分钟正常。我这次在干净检出的 HEAD 上跑出来是 `100% tests passed out of 4`，四个目标全过，`visor.tests` 用了 135.04 秒。

整串验收咱们一条命令跑完：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍代码，fontgen 的独立构建与测试收尾。你看末行，打印 `acceptance: all green` 才算过，中间任何一道停下都不算。

## 下一站的口子

单次 flush 的节奏没有判例，红绿实验里那个全绿的盲区咱们照实记在这里。彩色条带区域的 X11 像素读回仍没有，批式判例盖住的是单色。第三十九章类注释里挂着的交换缓冲还是 a later batch，下一章收回它，同一批还有一件更大的：把 Cinux 链接的全部层喂给 armv7 的交叉编译器，PC 半边的路铺到硬件跟前。

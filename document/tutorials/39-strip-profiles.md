# 第三十九章 条带进三档：光栅 profile 与字节预算

第三十八章结尾我们留了话：「条带还没进 traits：画布现在只有格式一维，缓冲的形状（整幅、水平条带、往后的交换缓冲）是后面的批次。」这一章收回这句话。条带通路从第十一章起就在，`render_window` 与 `render_strips` 一直在跑，信号台与贪吃蛇两个示例渲染用的就是 16 行的条带画布。可是我们把判例摊开数一遍就会发现，条带通路的全部判例只在单色档跑过，格式与条带两个维度从未合体。这一批把它们合成一件事验掉：同一套六场景在 Mono1、RGB565、XRGB8888 三档下各跑一遍，逐像素对齐整幅参考。画布这一侧跟着落了一个名字与一个数，后面两节各自讲。

## 光栅 profile：Size 加 Format 这一对

第十一章讲条带时我们说过：条带缓冲是编译期的，屏幕尺寸是运行期的，前者叫光栅 profile。这个名字早就在画笔与渲染循环的注释里，`render_strips` 说编译期的条带画布就是它，这一批画布的类注释把它收进来，还配上预算数。我们翻开 `include/visor/canvas/canvas.hpp`：

```cpp
/**
 * The Size/Format pair is the compile-time raster profile: a full frame, a
 * horizontal strip and the swap buffers of a later batch are this one template
 * in different configurations, never separate types. A build shops the profile
 * by buffer budget -- kBufferBytes is the number it shops with -- while the
 * screen size stays a runtime loop parameter that only says which windows to
 * render this round.
 */
```

顺着注释我们看三件事。profile 就落在 Size 加 Format 这一对模板参数上，整幅是 `Canvas<SizeI{.w = 480, .h = 320}, Rgb565Traits>`，条带只是把 320 换成 64，同一模板的两个实例，仓库里没有为条带另立过任何类。交换缓冲是这一对的第三种配置，注释里明说 a later batch，这一批没动它。挑 profile 掂量的是字节预算，这个数画布直接报出来：

```cpp
static constexpr std::size_t kStrideBytes = Format::bytes_per_row(kWidth);
/// The whole buffer in bytes: the budget a raster profile costs.
static constexpr std::size_t kBufferBytes = kStrideBytes * kHeight;
```

宽一行的字节数乘上行数，就是整块缓冲的字节数。到后面算预算的地方，我们直接拿这个数进断言。

## 关卡场景换角色

第十一章六道判例共用一张 20x20 的测试画面。底是全屏亮，一块暗色挖孔骑在条带 0/1 边界上，两个亮点藏在条带里。这一批我们把画面原样搬进 `tests/render_tests.cpp` 的 `GateScene`，演员表升了一档，两个标记不再只是亮点，一个填 `Accent`、一个填 `Border`，挖孔明确填 `Background`：

```cpp
hole.fill = visor::canvas::ColorRole::Background; // the dark hole is the point
tail_dot.fill = visor::canvas::ColorRole::Accent;
head_dot.fill = visor::canvas::ColorRole::Border;
```

几何一个像素没动，我们把三个成员的声明摆出来，你对着第十一章数：

```cpp
// Rows 6..11: crosses the strip boundary at row 8, catching replay slips.
Rectangle hole{RectI{.pos = PointI{4, 6}, .size = SizeI{.w = 6, .h = 6}}};
// Rows 16..20: lives entirely in the tail strip.
Rectangle tail_dot{RectI{.pos = PointI{12, 16}, .size = SizeI{.w = 4, .h = 4}}};
// Rows 2..6: lives entirely in the first strip.
Rectangle head_dot{RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}}};
```

你数下来：挖孔骑第 8 行的带界，两个标记整块地各住一条带，开场时都藏起来。场景升的是角色，不是几何。升档的用意写在类注释里：

```cpp
/// The markers carry Accent and Border so the color runs compare real
/// distinct values at the seams; a mono run folds both to lit, exactly
/// what the founding mono gate asserted.
```

单色那一档我们用 `kLightTheme` 跑，Accent 与 Border 都是白墨，`from_ink` 把两者折叠成亮，挖孔是黑墨不亮，第十一章立下的亮暗语义一个字没变。彩色两档用 `kColorTheme` 跑，整套演员表是四个互不相同的真值：底是近白的前景墨 0xe7ebf5，孔是纯黑，两个标记分别是鸢尾蓝 0x4a62c8 与蓝灰 0x39435c。两个标记不同帧可见，同屏最多同现三个，比较读到的每一个都是真值。

比较这一步我们交给宿主，读回的是本格式的原字：

```cpp
if (integral.get_pixel(PointI{x, y}) != striped.get_pixel(PointI{x, y})) {
    if (mismatches == 0) {
        std::println("strip gate first mismatch at ({}, {})", x, y);
    }
    ++mismatches;
}
```

`compare_integral_vs_strips` 把同一棵树渲染两遍，一遍整幅画布一次提交，一遍条带画布分带提交，两个宿主各按画布 traits 的 `kWireFormat` 开口，然后我们拿 400 个点逐个比。`get_pixel` 按宿主自己的格式答：Mono1 答零或一，RGB565 答 16 位字，XRGB8888 答 `0xAARRGGBB`。三种格式都是整词精确比较，打包在任何一道带缝上错一个字节，读回的字就不是同一个字，第一处失配的坐标当场打出来。

## 尾块与旧位恢复

六道场景我们照第十一章的顺序挨个过。模板 `run_strip_gate<Format>` 收一份主题，Mono1 喂单色预设、彩色两档喂彩色预设，六场景的代码在三种格式下一行不改，场景自己从来不知道格式是什么。

尾块那道：屏幕 20、条带 8，最后一个窗口只有四行，尾标记点亮。整幅一次提交，条带三次（8+8+4），提交次数本身就是断言。彩色下你再看末条带里那个标记，它是 Accent 的真值 0x4b19（鸢尾蓝落 565 的字，第三十八章算过）。尾窗口要是把缓冲里上一带的残留带进来，或者四行的窗口裁错一行，这个字就变了样。

旧位恢复那道：标记从末条带挪到顶部。我们另起一个宿主，按正规路收下一帧完整的 v1，它扮演一块真的持有旧像素的屏幕，三次提交。然后我们把可见性一换，新旧两个位置推进收集器：

```cpp
DirtyRegions moved;
moved.push(RectI{.pos = PointI{12, 16}, .size = SizeI{.w = 4, .h = 4}}); // old, strip 2
moved.push(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}});   // new, strip 0
```

我们数清单：只有两条记录，第 1 条带没有人申报，在家留守。脏区路径只重画第 0 与第 2 带，提交次数 3+2，结果与整幅的 v2 逐像素一致。彩色下这道多一层压力：旧位置留着一块 Accent 真值，得由重画的条带拿前景墨盖回去。哪条带偷懒不重画，宿主上就留一块鸢尾蓝，比对当场翻红。单色下同样漏画留下的是一块亮，两档都逃不过，彩色把错处指得更具体。

## 全屏、溢出与攒带

全屏失效那道最短：一次把整个屏幕推进收集器，三条带全重画，提交次数 3。我们三档各跑一遍，谁跑都是三，它守的是最粗的失效档。

溢出坍缩那道：我们把九个互不相交的一像素小矩形推进容量八的清单，代码是一个九次循环：

```cpp
for (int i = 0; i < 9; ++i) {
    scattered.push(RectI{.pos = PointI{i * 2, i * 2}, .size = SizeI{.w = 1, .h = 1}});
}
VISOR_CHECK_EQ(scattered.count(), std::size_t{2});
```

前八个折成一盒、第九个自占一格，两格合起来跨满三条带，该重画的带一个都没有少，像素照样逐点一致。彩色下我们把第十一章的分界再念一遍：折叠是正确性，合并是节俭。包围盒吞下去的干净像素在彩色下也是真值，多画不扣分，漏画才是事故。

攒带拨盘那道：同一个循环我们换画布，拨盘就是模板参数：

```cpp
using Half = Canvas<SizeI{.w = kScreen, .h = 16}, Format>;
using Slim = Canvas<SizeI{.w = kScreen, .h = 5}, Format>;
```

你数提交：16 行的条带两次（16+4，尾窗口只占缓冲十六行里的四行），5 行的四次，挖孔正好骑在第二条边界第 10 行上。彩色下这道守的是重放纪律：第 10 行的缝两次进入同一棵树，哪一遍画岔了，缝上的字就换了一片。两档都必须与整幅逐像素一致，条带高度这个编译期拨盘转到哪一档，循环一行不改。

## 奇 x 补丁：从移位拷贝到整像素偏移

我们最后看第六道，它在第十一章叫奇数位提交，区域从 x=3 起步：

```cpp
using Patch = Canvas<SizeI{.w = 8, .h = 4}, Format>;
const RectI patch_window{.pos = PointI{3, 5}, .size = SizeI{.w = 7, .h = 3}};
```

单色下 x=3 意味着源与目标的位号错开三位，宿主只能逐位搬运。彩色下我们再看同一道场景，它换了面孔：x=3 是整像素偏移。宿主收彩色区域走的是整行字节段拷贝，目标列就是 x 乘每像素字节数（RGB565 乘 2，XRGB8888 乘 4），没有移位这回事。要验的换成画笔的窗口平移落得准不准：场景 (3,5) 要落进缓冲 (0,0)，挖孔的第一个暗像素场景 (4,6) 要落进缓冲 (1,1)，窗口外的第 7 列与第 3 行是缓冲的多余位，保持 `set_clear(false)` 留下的本格式黑。

```cpp
VISOR_CHECK(patch.get(PointI{0, 0}) == ground_pixel);
VISOR_CHECK(patch.get(PointI{1, 1}) == hole_pixel);
VISOR_CHECK(patch.get(PointI{7, 0}) == dark_pixel);
VISOR_CHECK(patch.get(PointI{0, 3}) == dark_pixel);
```

四个探针我们按本格式的像素值比。孔的暗与多余位的黑是两个名字、两个来历，一个出自角色表，一个出自清底，值在这里恰好相同。补丁宿主收下整帧，再补这一块，最后与整幅参考 400 点对齐。同一道场景在移位与偏移两副面孔下都得过，这就是三档各跑一遍的意思。

## 彩色端到端：六轮对整幅参考

关卡守的是渲染原语，端到端守的是一轮一轮的节奏。老的单色管线我们用逐点布尔探针守：

```cpp
VISOR_CHECK(screen.get(PointI{2, 2}) == false); // the ring went out
VISOR_CHECK(screen.get(PointI{13, 17}) == false); // the press frame went out
```

彩色下我们不能照搬这套探针，原因写在模板注释里：同一根探针会把控件的内部用色硬编码进判例。环用什么墨、芯用什么墨是控件与主题之间的事，判例写死一个字，主题一换全红。

所以彩色管线换一套守法，两件套是状态断言加整幅对齐。每轮结束我们问树要状态（`focused`、`checked`、`pressed`、开火计数），再把此刻的树整幅渲染一遍当参考，与条带宿主的屏逐像素对齐：

```cpp
const auto parity = [&] {
    visor::host::OffscreenHost reference(kScreen, kScreen, visor::host::kWireFormat<Format>);
    Full frame;
    frame.set_clear(false);
    visor::ui::render_window(ground, frame, kWholeScreen, reference.table());
    compare_hosts(reference, screen, kScreen, kScreen);
};
```

我们把判断的落点从「这一点该是这个值」换成「整幅必须是这棵树此刻的样子」，控件内部怎么用色不再进判例。焦点环、勾选、按压各自的表现都还在断言里，只是改问树要状态，像素那一层交给整幅对齐。

第一帧不占轮次，按第十二章立的口径它是调用者的义务：`render_strips` 全量铺一遍，三次提交。之后六轮我们一轮一轮数。Tab 聚焦复选框，一条带重建，四次。Space 勾选，五次。Tab 把焦点走到末条带的按钮，退环与进环分住两条带，第 0 与第 2 条重画、第 1 条在家，七次。Enter 按下，八次，开火计数仍是零。Enter 抬起，同一条带再画，九次，开火计数一。一轮一键的节奏照旧，按下与抬起永不同轮，所以按压帧独自占一轮重建。第六轮空转，没有事件没有申报，一次都不提交，计数停在九。清单每轮清空，RGB565 与 XRGB8888 两档各跑一遍这套，每轮之后 parity 对齐。

## 站位算术：预算表写进 static_assert

三档关卡全绿之后，我们再收一张表，480x320 的屏（信号台起的示例屏，测试与示例的注释叫它 station size，站位尺寸）配 64 行的带，三档格式各花多少字节。表直接写进判例，逐格写成 `static_assert`：

```cpp
using MonoFrame480 = Canvas<SizeI{.w = 480, .h = 320}>;
using MonoStripe64 = Canvas<SizeI{.w = 480, .h = 64}>;
static_assert(MonoFrame480::kStrideBytes == 60);
static_assert(MonoFrame480::kBufferBytes == 19200); // 18.75 KiB
static_assert(MonoStripe64::kBufferBytes == 3840);  // 3.75 KiB
static_assert(Rgb565Frame480::kStrideBytes == 960);
static_assert(Rgb565Frame480::kBufferBytes == 307200); // 300 KiB
static_assert(Rgb565Stripe64::kBufferBytes == 61440);  // 60 KiB
static_assert(Xrgb8888Frame480::kBufferBytes == 614400); // 600 KiB
static_assert(Xrgb8888Stripe64::kBufferBytes == 122880); // 120 KiB
```

单色这行你手算就能对上：480 除 8 正好 60 字节一行，乘 320 行是 19200 字节。彩色两档的 using 同款，各起整幅与 64 行条带一对名字。三档的条带缓冲都是整幅的五分之一，比例只跟 64 比 320 走，跟格式无关。KiB 按 1024 换算，注释跟着断言一起进了编译期。

你拿这几个数掂一个小 RAM 构建的 profile：彩色整幅一张 300 KiB 起，换成 64 行条带压到五分之一，同一棵树、同一个循环，换的只是模板参数。往后谁改了打包公式或者 `kBufferBytes` 的算式，构建当场停，不用等测试跑。

## 站位实弹：480x320 五整带与真尾块

算术之外我们再打一发实弹，480x320、RGB565、彩色预设。选这组几何是因为两种带形都能从它裁出来：320 正好是 64 的五倍，64 行的带画布五条整带、没有尾块，50 行的带画布补一个真尾块，六条 50 行加一条 20 行。画面是站位版的三件套：全屏底，一块挖孔从第 40 行铺到 139 行，跨过 64 与 128 两道带缝，一块 Accent 标记在第 300 到 315 行，整块住在末条带里。

两条路我们都与整幅参考比过，153600 个点逐像素一致，五整带提交五次、带尾块那路七次。然后是那发按整词的断言，Accent 的字原样读回：

```cpp
VISOR_CHECK_EQ(banded.get_pixel(PointI{452, 308}),
               visor::canvas::Rgb565Traits::from_ink(visor::ui::kColorTheme.accent));
```

你看 (452,308)，它是标记中心。鸢尾蓝 0x4a62c8 落 565 是 0x4b19，我们拿这个断言按整词比：三个通道任何一位错位、高低字节对调、哪一道带缝上丢了一行，读回来的都不是这个词，通道顺序跟着这个字一起验掉。旁边两根探针点住另外两个角色，底上 (10,10) 读回前景墨的整词，孔里 (130,50) 读回 0。

## --strip：条带上桌面

判例与断言都在离屏，我们也要在桌面亲眼看一次条带喂图的样子。示例 `examples/x11_window` 多收一个 `--strip`，与 `--color` 任意组合，四条路：

```bash
./build/examples/x11_window                  # 单色整幅
./build/examples/x11_window --color          # RGB565 整幅
./build/examples/x11_window --strip          # 单色 64 行条带
./build/examples/x11_window --color --strip  # RGB565 64 行条带
```

示例里跑窗口循环的 `run_window` 本来就是模板，画布是它的参数：你传整幅画布，每次重建提交一个区域，你传 64 行条带画布，同一个 `run_dirty` 把屏按带扫完。宿主那边不跟着变，X11 的影子帧还是整幅地记着屏幕，条带只是让喂进来的区域变小、次数变多。条带路启动多打一行说明：

```cpp
if constexpr (Frame::kHeight < kScreen.h) {
    std::println("visor: strip raster, {} rows per band", Frame::kHeight);
}
```

WSLg 里 DISPLAY 已指好，你直接跑，或者明写：

```bash
DISPLAY=:0 ./build/examples/x11_window --color --strip
```

成功条件你看得见：终端打出 `visor: strip raster, 64 rows per band`，跟着打出操作提示，窗口里是同一张彩色控件页，Tab 走焦点、Enter 激活，与整幅路一个手感。桌面一侧没有像素读回，画面对不对靠肉眼看，正确性由离屏判例看住。没有桌面环境的机器可以自己起一台 Xvfb 冒烟，`Xvfb :99 &` 之后 `DISPLAY=:99` 跑同一条命令。

## 这一批没做的事

没做的事我们照实记。交换缓冲这第三种配置没动，类注释里它还是 a later batch，这一批验的是整幅与条带两种配置在三档格式下的合体。

X11 宿主收条带区域没有像素级读回的判例，单色那边奇 x 部分区域配逐像素读回的判例一直在。彩色回读那组判例真正盖到的只有整幅，条带喂图的正确性由离屏判例看住，桌面上靠肉眼。SPI 真彩屏的板端验证仍是下一站，站位算术与站位实弹都是提前算好、提前验掉的那部分。桌面示例还是 Mono1 与 RGB565 两档，你跑示例见不到 XRGB8888 的窗口。

## 验收

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件是汇总行以 `100% tests passed` 开头、退出码 0。这一章的判例都编在 `visor.tests` 里，X11 判例要自己拉 Xvfb，整串跑两三分钟正常。我这次跑出来是 `100% tests passed out of 4`，四个目标全过。

整串验收我们一条命令跑完：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍代码，fontgen 的独立构建与测试收尾。你看末行，打印 `acceptance: all green` 才算过，中间任何一道停下都不算。

红绿实验两个，你可以自己做。第一个做在 RGB565 的字节序上：把 `include/visor/canvas/pixel_traits.hpp` 里 `Rgb565Traits::store` 的高低字节对调，`row[x * 2]` 写高字节、`row[(x * 2) + 1]` 写低字节，重新构建跑测试。红的集中在一类比较上，「经内存读回的字」对「`from_ink` 直算的字」：像素判例里 RGB565 的字节序、鸢尾蓝读回、整幅 fill 逐像素与离屏宿主读回那一串都红，条带关卡第六场景 ground 那根探针红，站位实弹的 Accent 与前景两根整词探针红。X11 的彩色回读也红，纯红整帧的字 0xf800 对调后成了 0x00f8，左上与右下角两根读回都对不上红掩码。不红的也有，而且更有意思：黑值探针四根全绿，关卡第六场景三根之外，站位孔里 (130,50) 读回 0 那根同样是黑值，0x0000 高低字节对调还是它自己。整幅对条带的比对也不红，两头都经同一条写读路，一起换位，比不出错。这个盲区值得看清：字节序要看得住，断言必须一端直算、一端经内存。Mono1 与 XRGB8888 两档照旧绿，它们的像素各自走自己的 store。改回去，恢复绿。

第二个你做在预算数上：把 `canvas.hpp` 里 `kBufferBytes` 的算式抹掉 `kHeight`，写成 `kStrideBytes`，重新构建。这次测试一个都不用跑，编译就停：那排 static_assert 第一条就过不去，Mono1 整幅的 `kBufferBytes` 成了 60，断言要 19200，`static assertion failed` 打在构建输出里，指到 `render_tests.cpp` 这几行。画布自己的存储数组用的是另一个表达式，不受这次改动影响，红的全是断言。预算数是承诺，承诺在编译期就有人验。改回去，恢复绿。

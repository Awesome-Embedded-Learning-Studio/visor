# 第七章 差分关卡：五矩形、三轮比对、消毒器与第一张留存下来的图

前面六章各自交了货，这一章把它们串起来过一次关卡。测试文件是 `tests/diff_tests.cpp`，它不实现任何新东西，只做一件事：画一个由五个矩形组成的场景，然后拿三份各自独立得到的答案互相比对。这一章结束时，普通构建与 ASan/UBSan 构建全部通过，项目产出第一张留存下来的图片，你会知道它该是什么样子、又该怎么正确地看它。

## 五个矩形，五种处境

场景在 128×64 的画布上（常见单色 OLED 的分辨率），五个矩形一次把绘制边界会遇到的处境占全：

```cpp
// The five areas of the gate scene, exactly as the plan spells them: one plain,
// one overlapping the first, one clipped at the bottom-right, one clipped at the
// negative origin, one fully outside the canvas.
constexpr std::array<visor::base::RectI, 5> kScene{{
    {.pos = visor::base::PointI{8, 4}, .size = visor::base::SizeI{.w = 40, .h = 20}},
    {.pos = visor::base::PointI{30, 10}, .size = visor::base::SizeI{.w = 60, .h = 30}},
    {.pos = visor::base::PointI{100, 50}, .size = visor::base::SizeI{.w = 60, .h = 30}},
    {.pos = visor::base::PointI{-10, -10}, .size = visor::base::SizeI{.w = 30, .h = 30}},
    {.pos = visor::base::PointI{130, 10}, .size = visor::base::SizeI{.w = 5, .h = 5}},
}};
```

第一个 `{8,4,40,20}` 是老老实实待在画布里的矩形。第二个 `{30,10,60,30}` 与它重叠，重叠区被两次 `fill` 覆盖，结果必须与画一次完全相同。第三个 `{100,50,60,30}` 右下角探出画布，画布之外的部分该被裁掉。第四个 `{-10,-10,30,30}` 原点是负的，左上角被裁，落在画布里的 `[0,20)×[0,20)` 该亮。第五个 `{130,10,5,5}` 整个在画布外面，应当什么都不留。

## 参考图：不画图的裁判

差分测试需要一个独立的裁判。这里的裁判是纯坐标的：

```cpp
/// The pure coordinate reference: no drawing involved, just containment checks.
/// Clipped areas need no special casing, a point outside the canvas is outside
/// every area by construction.
constexpr bool expected_on(int x, int y) {
    const visor::base::PointI p{x, y};
    return std::ranges::any_of(kScene, [&](const auto& area) { return area.contains(p); });
}
```

`expected_on(x, y)` 只问一件事：这个点落在五个矩形的任何一个里吗。它不经过画布、不经过笔、不经过宿主，实现再怎么坏也污染不到它。注释还点破一个巧处：裁剪不需要特判。矩形探出画布的部分自然不被画布上的任何点问到，负原点也一样，参考图天然只覆盖画布内且属于场景的点。

## 三轮比对

画图与交接的代码很省：

```cpp
    // Paint the scene with one pen, five fills.
    GateCanvas canvas;
    const Pen pen{canvas, Color::On};
    for (const auto& area : kScene) {
        pen.fill(area);
    }

    // Hand the finished frame to the offscreen host, the second reader in the ring.
    OffscreenHost mirror(128, 64);
    VISOR_CHECK(visor::host::submit_frame(mirror.table(), canvas));
```

画布用的是测试开头起的别名 `GateCanvas`（128×64）。一支笔五次 `fill`，整幅交给第六章的离屏宿主。然后是三轮比对，8192 个像素全部过堂：

```cpp
    std::size_t canvas_vs_reference = 0;
    std::size_t host_vs_reference = 0;
    std::size_t host_vs_canvas = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) {
            const Color from_canvas = canvas.get(PointI{x, y});
            const Color from_host = mirror.get(PointI{x, y});
            const Color reference = expected_on(x, y) ? Color::On : Color::Off;
            if (from_canvas != reference) {
                if (canvas_vs_reference == 0) {
                    std::println("canvas first mismatch at ({}, {})", x, y);
                }
                ++canvas_vs_reference;
            }
            if (from_host != reference) {
                if (host_vs_reference == 0) {
                    std::println("host first mismatch at ({}, {})", x, y);
                }
                ++host_vs_reference;
            }
            if (from_host != from_canvas) {
                if (host_vs_canvas == 0) {
                    std::println("host vs canvas first mismatch at ({}, {})", x, y);
                }
                ++host_vs_canvas;
            }
        }
    }
    VISOR_CHECK_EQ(canvas_vs_reference, std::size_t{0});
    VISOR_CHECK_EQ(host_vs_reference, std::size_t{0});
    VISOR_CHECK_EQ(host_vs_canvas, std::size_t{0});
```

为什么是三轮，一轮不够吗？

第一轮，画布对参考。它抓绘制侧的错：裁剪错了、重叠区画花了、半开区间边界理解错了，全在这一轮暴露。第二轮，宿主收到的帧对参考。它抓交接侧的错：行距错了、拒收逻辑错了、解码比特错了。第三轮，宿主对画布。前两轮的裁判都是同一张参考图，假如参考图自己错了（场景列表写错），前两轮一起瞎。第三轮不依赖参考图，它只问交接是否忠实：只要宿主与画布一致，画了什么就收到了什么。

三轮各守一边，谁也替代不了谁。每个像素的首次不一致还会打印坐标，红的时候你拿到的不只是“败了”，是“第一个坏点在哪”。

## 消毒器关卡

普通构建绿了，关卡还有第二道。ASan 与 UBSan（第一章配好的 `VISOR_SANITIZE` 开关）在独立的构建目录里，把同一套测试再跑一遍。为什么要独立目录？消毒器给每个内存访问插桩，编译产物完全不同，与普通构建混在一个目录里会互相污染，两个目录各干各的：

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

这是项目第一次开消毒器目录，结果零报告。越界读写、悬垂指针、未定义行为，差分测试用断言抓不到的整类问题归它管。两种构建都通过，B1 的验证才算走完。

## 第一张留存下来的图，以及它的正确读法

差分测试的最后一步是把成果写成一张图片留档：

```cpp
    // The graduation photo: the first image this project produces.
    VISOR_CHECK(mirror.save_pbm("visor_first_frame.pbm"));
```

文件落在测试的工作目录 `build/tests/visor_first_frame.pbm`（ASan 目录下也会有一份）。大多数认 PBM 的图片查看器都能打开它，GIMP 一类图像软件也行。

看图之前有一条要提前说明，不然你会以为画反了：PBM 约定位 1 渲染为黑。画布里的 `Color::On`（亮）在图片里是黑块，背景是白。所以你看到的应该是一张白底图，黑块按场景对号：

- 左上一大片黑，由三个矩形糊成：负原点矩形裁剩的 `[0,20)×[0,20)` 贴着左上角，第一个矩形 `[8,48)×[4,24)` 与它在 `x∈[8,20)、y∈[4,20)` 处重叠，第二个更大的矩形 `[30,90)×[10,40)` 又叠上来，三者连成一片。
- 右下角一小块被画布边缘切齐的黑块，切口就是画布边界（第三个矩形 `[100,160)×[50,80)` 被裁成 `[100,128)×[50,64)`）。
- 第五个矩形在图上无影无踪。它整个在画布外，本来就该什么都不留。

拿这张图和 `kScene` 的五个坐标对看一遍，每一块都能对上号，这是差分测试之外肉眼级的复核。

## 验收

两种构建全部通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

两次都报 `visor.tests` 通过。

然后做两个实验。

实验一，亲手弄坏它。编辑 `tests/diff_tests.cpp`，让画图的循环跳过第一个矩形：

```cpp
    for (const auto& area : kScene) {
        if (area.pos.x == 8) {
            continue;
        }
        pen.fill(area);
    }
```

重新构建并跑测试，你会看到两行输出，`canvas first mismatch at (20, 4)` 与 `host first mismatch at (20, 4)`，第三轮保持零（宿主忠实地收到了一个少画了一块的画布，交接本身无错）。两个计数不再是零，测试红。

第一个坏点为什么是 `(20, 4)` 而不是第一个矩形的左上角 `(8, 4)`？因为 `(8, 4)` 同时落在负原点矩形裁剩的 `[0,20)×[0,20)` 里，那一块照亮了它，画布与参考在此处一致。第一个只属于被跳过矩形的点，是 `y=4` 行上 `x=20`：负原点矩形的覆盖到 `x<20` 为止，第二个矩形 `y` 从 10 才开始，这里只剩参考说亮、画布没亮。这演示了三轮分工：画布错了，第一二轮红，第三轮绿。

实验二，往 `kScene` 里添第六个矩形，比如左上 `(60, 40)`、宽高 `30×20` 的一个，画图与参考同源，测试照样通过。这是这套裁判的边界：它验证“画出来的等于场景描述”，场景描述本身的正确性它管不了。差分测试不是万能裁判，它是把“实现与规格的一致性”这件事自动化，规格自身的对错永远要人看。两个实验做完都改回去，最终确认两种构建都通过。

B1 到此收工。计划中的下一站：事件与时钟进宿主表、脏区域收集与合并、控件树、RGB565 光栅 profile、三桌面窗口宿主（Linux、Windows、macOS）、真实 OLED 开发板，以及 fontgen 的取模转换逻辑。资源口径（Flash、静态 RAM、栈峰值、传输量、耗时）从接真实 OLED 开发板的批次起记录，PC 批次不适用。教程会跟着批次继续更新。

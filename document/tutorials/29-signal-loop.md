# 第二十九章 信号台与帧循环

第二十八章的展示页把控件凑齐了，但整页是静的。这一章我们让同一套 1bpp 控件动起来：`examples/signal` 是一台 480×320 的单色信号台，左边示波区，右边模式与读数，下面四个旋钮和 RUN/HOLD、INVERT 两个按钮。动画不写在 `on_draw` 里，帧循环在宿主时钟上走。波形由定点正弦表生成，因为随帧变化的只有示波区，所以脏区只申报示波区。顺手补上第九个控件 `Knob`。

## 先看结果

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
DISPLAY=:0 ./build/examples/signal
```

窗口按两倍显示为 960×640，逻辑画布仍是 480×320。你看到的左边是 304×180 的示波区，点状网格上画实时波形。右边是 XY / YT / SPEC 三个模式。读数摆的是 FREQ / AMP / PHASE / GLOW。下面四个旋钮，拖动改变频率、幅度、相位与余辉。右下 RUN/HOLD 冻结相位，INVERT 在夜黑与纸白之间切换。Tab 遍历焦点，方向键也能调旋钮，Enter 或 Space 按按钮，关闭窗口即退出。

`--snapshot` 不需要显示服务，在固定相位出 PBM，咱们在没有 X 的机器上也能跑：

```bash
./build/examples/signal --snapshot build/visor-signal.pbm --at-ms 1200
./build/examples/signal --snapshot build/visor-signal-yt.pbm --demo --at-ms 5000
./build/examples/signal --snapshot build/visor-signal-spec.pbm --demo --at-ms 9000
./build/examples/signal --snapshot build/visor-signal-paper.pbm --paper --at-ms 1200
```

`--at-ms` 按固定的 16 毫秒节拍推进，同一毫秒数在每台机器上出同一张图。`--demo` 让模式、频率与相位自动巡演，`--paper` 选纸白。PBM 的 1 表示黑，而画布的 `Color::On` 表示点亮，所以导出时我们仍按第二十八章的 `255 - byte` 反转极性。

`--self-test` 是这一章的离屏验收，不需要 X，你直接跑：

```bash
./build/examples/signal --self-test
```

## 动画必须住在绘制外面

这一章动画要推进状态，第九章的约定不让 `on_draw` 干这件事：它只读状态、只画像素，不读事件、不推进动画、不改场景。咱们不妨假想一个偷懒的写法：画的时候顺手把相位加一。条带重放会把同一棵树画很多遍，每条带看到的相位都不一样，条带之间就对不上，画面会从边界开始撕裂。

所以我们把相位的写入权收拢到一个人手里：`Scope::tick`。`on_draw` 只读 `phase_`、模式与频谱的峰值。面板的帧循环 tick 完，再让渲染器重画：

```cpp
void SignalPanel::tick(std::uint64_t dt_ms) noexcept {
    if (demo_) {
        advance_demo(dt_ms);
    }
    scope_.tick(dt_ms);
}
```

咱们接着往下看 `Scope::tick`：它自己也不碰像素，只改状态，然后把示波区申报成欠画：

```cpp
void Scope::tick(std::uint64_t dt_ms) noexcept {
    if (!running_) {
        return;
    }
    const std::uint64_t capped = std::min<std::uint64_t>(dt_ms, 100);
    const auto advance =
        static_cast<std::uint32_t>(capped * static_cast<std::uint64_t>(1 + frequency_));
    phase_ = (phase_ + advance) & static_cast<std::uint32_t>(kPhaseMask);
    if (mode_ == Mode::Spectrum) {
        update_spectrum();
    }
    invalidate(bounds());
}
```

我想请你注意最后一行的 `invalidate(bounds())`：欠的是 `bounds()` 这一块，不是整屏。HOLD 时函数在 `invalidate` 之前就返回，一帧像素也不欠。冻结的画面本来就是对的，我们不需要把它擦掉重画。

旋钮改参数时也不直接画。控件的值回调把新值送进 `Scope::set_frequency` 一类的口子，设置函数改状态、申报示波区，面板再把右侧读数区一起申报。我们把这些都安排在 `on_draw` 之外，绘制只负责把当前状态重放出来。

## 一轮帧循环

主循环把「读时钟、推进、收事件、重画」串成一条线。第一帧是调用者的活，`run_dirty` 只重画有人申报的条带，所以我们在开窗前把整屏申报一次：

```cpp
Canvas<SizeI{.w = 480, .h = 16}> strip;
dirty.push(kScreenBounds);
visor::ui::render_strips(panel, strip, kScreen, host.table(), dirty);
dirty.clear();

auto last = visor::host::current_time_ms(host.table());
while (!host.close_requested()) {
    const auto now = visor::host::current_time_ms(host.table());
    std::uint64_t dt = now - last;
    last = now;
    dt = std::min<std::uint64_t>(dt, 100);
    panel.tick(dt);
    static_cast<void>(visor::ui::run_dirty(host.table(), panel, input, strip, kScreen, dirty));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
}
```

这一轮里有三个细节，咱们停下来逐个看：

- `dt` 用无符号减法算，时钟回绕也正确，这是第八章定下的约定，我们照着用。`std::min` 把一次长时间停顿夹到 100 毫秒，你把窗口拖住、机器卡一下，相位也不会瞬移。
- `run_dirty` 每轮最多吃一个键盘事件，指针事件免费排空，相位推进每轮都发生，事件泵和动画不抢同一条通道。咱们按键时，按下与抬起仍由结构保证分在不同轮。
- 这里没有用 `run_once`：`run_once` 按整幅画布画，脏区与条带都白费。我们把条带高度取 16 行，共 20 条带。示波区纵向 58 到 238，跨 12 条带，其余 8 条（页眉、按钮与旋钮、页脚）在动画帧里不动。

时钟推进的是真实时间，不是「第几帧」。咱们拿渲染慢一拍来试：下一轮的 `dt` 就大一点，波形仍走到该在的位置，只有超过 100 毫秒的停顿才被截断。

## 定点正弦

光栅化不碰浮点，波形的三角也要用整数。示例里我们放了一张 256 项的正弦表，值域是 ±4096，生成器是 Bhaskara I 的近似：

```cpp
constexpr int sine_degrees(int degrees) noexcept {
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    int sign = 1;
    if (d >= 180) {
        d -= 180;
        sign = -1;
    }
    if (d > 90) {
        d = 180 - d;
    }
    const int product = d * (180 - d);
    return sign * ((4 * product * kSineScale) / (40500 - product));
}
```

`sin(d) ≈ 4d(180-d) / (40500 - d(180-d))` 在 0、30、90 度上是精确的，其余位置误差在千分之二以内，我们拿它喂一个 304 像素宽的示波区，足够。表在编译期生成，运行时只查表。`sine_at` 把 0..4095 的相位折进 256 项，再在相邻两项之间做 1/16 的线性插值：

```cpp
int Scope::sine_at(int pos) noexcept {
    const auto wrapped =
        static_cast<std::uint32_t>(pos + kSineScale) & static_cast<std::uint32_t>(kPhaseMask);
    const auto index = static_cast<std::size_t>(wrapped >> 4U);
    const int fraction = static_cast<int>(wrapped & 15U);
    const int first = kSineTable[index];
    const int second = kSineTable[(index + 1) % kSineTable.size()];
    return ((first * (16 - fraction)) + (second * fraction)) / 16;
}
```

`+ kSineScale` 让余辉的负相位也落在表的下标范围里。掩码是唯一的回绕点，所有波形共用，排查相位问题时我们只看它。

XY 模式把两个正弦拼成李萨如曲线，我们取横轴 `a = 1 + freq`、纵轴 `b = 2 + freq`，相位偏移来自 PHASE 旋钮：

```cpp
const int x = cx + ((rx * ((amplitude * sx) / kSineScale)) / kSineScale);
const int y = cy + ((ry * ((amplitude * sy) / kSineScale)) / kSineScale);
```

这里分两次除法不是啰嗦，我们算过：`rx * amplitude * sx` 最大会到 150×4096×4096，超过 32 位。除一次把正弦压回 ±4096，再乘半径，整数就不会溢出。曲线取 1600 个采样点，点与点之间用两像素宽的 Bresenham 线段连起来，避免大频率比时出现断点。余辉是把相位往回退 160 的整数倍，退得越远采样越稀，形成密度渐弱的拖影。

YT 模式走另一条路：咱们每个 x 列算一个样本，相邻样本之间用一段两像素宽的竖线连接，1 到 8 个周期横跨整个屏幕。SPEC 模式把两条正弦加一个直流底座折成 24 根柱子，峰值线单独保存，每帧按固定步长衰减。频谱的柱高和峰值都在 `tick` 里算好，`on_draw` 只读 `levels_` 与 `peaks_`，我们让绘制继续保持无副作用。

## 只重画该重画的地方

示波区是 304×180，一次 tick 申报的就是它。16 行条带下，纵向 58 到 238 落在 12 条带里。咱们再数一遍其余部分。页眉在 0 到 50，旋钮与按钮在 248 以下，页脚在 300 以下，三块都不在申报范围内。同住一条带的邻居躲不开顺带重画。页眉的横线在 50 行，与示波区顶的 58 行同住 48 到 63 行的条带，重放时它会跟着再画一遍，但那是条带重放，动画没有申报它们。HOLD 时连这 12 条也不碰，欠画清单是空的，`run_dirty` 不提交任何区域。

旋钮改变参数时，回调申报示波区与右侧读数区两块。读数区在示波区的同一批条带里，所以这一轮会顺带把右侧的静态文字重画一遍。这是条带模型的诚实成本：脏区是横向条带，不是矩形。页眉的其余部分与底部控件仍然不动，我们省下的部分是真的省下。

主题切换是相反的极端：`set_theme` 沿树给每个控件申报自己的 bounds，欠画列表装不下时坍缩成包围盒，整屏重画一次。咱们拿一次昂贵的全屏翻转，换即时的配色切换，在桌面宿主上划算。小屏上要不要缓一帧，我们把这个问题留在下一站。

## 顺手补一个旋钮

信号台有四个连续参数，滑块当然能用，但咱们做的是仪器，面板上该是旋钮。`Knob` 是这一章补进 `ui` 层的第九个控件，接口保持和其他控件一样的形状：

```cpp
Knob(base::RectI bounds, int minimum, int maximum, int value) noexcept;
void set_value(int value) noexcept;
void set_on_value(ValueHandler handler, void* context = nullptr) noexcept;
```

`set_value` 一进来就把值夹到范围，值没变就安静返回，变了才申报并回调。我们让它和 Checkbox、Slider 保持一致，同样遵守「同值不欠画」。键盘左右上下各走一步。

鼠标按的是角度，不是位移。你按在盘面哪一侧，指针就转到那个角度，拖着绕圈，指针跟着手走，顺时针加、逆时针减。方框四角也能按，方框是命中测试的单位，角落有角度，只有正中心没有方向，按了不动。缺口在六点钟的 90 度里，指针不绕过去，停在更近的一端：

```cpp
void Knob::update_value(base::PointI at) noexcept {
    const PointI middle = center();
    const int dx = at.x - middle.x;
    const int dy = at.y - middle.y;
    if (dx == 0 && dy == 0) {
        return;
    }
    const int index = direction_index(dx, dy);
    int steps = index - kDialStartIndex;
    if (steps < 0) {
        steps += kAngleSteps;
    }
    if (steps > kFaceSteps) {
        const int gap = steps - kFaceSteps;
        steps = gap <= kGapHalf ? kFaceSteps : 0;
    }
    const int span = maximum_ - minimum_;
    set_value(minimum_ + ((steps * span) / kFaceSteps));
}
```

`direction_index` 是正弦表的反查：256 个方向里挑与 `(dx, dy)` 点积最大的那个，相当于我们用整数做了一遍 atan2，不引 libm。表盘从 135 度顺时针扫 192 个方向步到 405 度，剩下的 64 步是缺口。`steps` 落在缺口里时，按离哪端近来夹到最小或最大。

这一版换掉了最初的相对纵向拖拽。纵向拖拽在表盘两侧还算顺手，但指针一过十二点或四点半，你的手往上、指针却往右下走，咱们读起来就是「反向」。首版同时只认圆盘内按下，方框四角按不动。角度定位的代价是按下即跳到该角度，没有相对拖拽的「不跳值」，换来的是指针永远跟手、四个角都能抓、方向不再随象限翻。

我们绘制时没有新增任何 `Painter` 动词：外圆用 `fill_rounded` 填前景，内圆用背景色再填一次，剩下的就是一圈。11 个刻度与指针端点都用同一套定点正弦近似算角度，指针用两像素 Bresenham 画出来，中心补一个三像素的轴。表盘从 135 度扫到 405 度，缺口落在六点钟方向，最小在左下、最大在右下。

判例覆盖了值的夹取、同值静默、键盘步进、东南西北四个方向的角度定位、缺口夹取、方框左上角可抓、正中心不动，以及真像素上至少 40 个亮点与中心轴。主题判例把它加进控件清单，参与两档极性的逐像素互补和 5 行条带重放。`kMaxChildren` 还是 8，面板的子件正好是示波区、模式组、四个旋钮、两个按钮，八个。旋钮上方的文字是面板自己画的，不占子件名额，我们数子件时不用算它。

## 快照与自测

快照的时间由 `advance_for` 推进，我们固定 16 毫秒一拍：

```cpp
void SignalPanel::advance_for(std::uint64_t ms) noexcept {
    constexpr std::uint64_t step_size = 16;
    while (ms > 0) {
        const std::uint64_t step = ms > step_size ? step_size : ms;
        tick(step);
        ms -= step;
    }
}
```

`--snapshot` 把 `advance_for(at_ms)` 跑完，再用整幅画布 `draw` 一次，写 PBM。相位是整数的函数，没有浮点累积误差，所以你拿同样的 `--at-ms` 跑，每次出同样的图。`--demo` 的巡演也走整数毫秒，模式在 4 秒、频率在 1.2 秒的边界上换。

`--self-test` 在离屏宿主上跑四道判断，咱们逐条看：

1. 整幅绘制与 16 行条带重放逐像素一致。
2. 运行中的 tick 只欠示波区，不欠页眉和读数区。
3. HOLD 时 tick 一像素也不欠。
4. 在 FREQ 旋钮北侧按下、顺时针转到东侧、松手，示波区的像素必须变化。它把「事件进、旋钮值变、示波区重画」的整条链路替我们走了一遍。

第四道在当前代码上改变了 9943 个示波区像素。它不碰面板的私有状态，只看宿主收下的像素，所以以后我们换布局或换波形，判断仍然成立。这个自测注册成 CTest 的 `visor.signal.selftest`，验收脚本会跑它。它不需要显示服务，`OffscreenHost` 就够了。

## 判例与探针实录

第一次链接是红的：示例里的自测用了 `OffscreenHost`，而 `examples/CMakeLists.txt` 只链了 `visor.host.x11`、`visor.host`、`visor.ui`，链接器把 `OffscreenHost` 的构造、`table`、`get`、`push_event` 全列了出来。我们补上 `visor.host.offscreen` 之后才绿。离屏宿主是 PC 判例的家当，示例一旦自测就要把它当依赖写清楚，不能靠测试目标替它藏着。

第一次 `clang-tidy` 也红了几处，都是口径问题。枚举常量用了 `kXy`，检查要求 PascalCase，我们改成了 `Xy`。正弦插值里 `wrapped >> 4` 的右操作数是带符号的 `4`，改成 `4U`。`advance_for` 里的局部常量叫 `kStep`，而局部常量按变量口径用 snake_case，改成 `step_size`。读数里的嵌套三目被要求拆成 `if/else`。主循环的 `if (dt > 100) dt = 100;` 被要求写成 `std::min`。判例里只读的 `Knob drawable` 漏了 `const`。我们全部按检查修正，没有停任何一项。

旋钮的第一版是相对纵向拖拽，作者上手后报「有时候反向、有时候点不动」。我们查下来，反向的根源是表盘几何，跟符号没有关系：指针顺时针转，纵向上拉在右半边就会把指针往右下带。点不动是圆盘判定把方框四角排除在外。第二版改成按角度定位，判例从上下拖拽改成东南西北定位、缺口夹取、四角可抓、中心不动，自测也从「向上拖 32 像素」改成「北侧按下、顺时针转到东侧」。

两根红绿探针，你可以自己动手做：

- 把 `Scope::tick` 末尾的 `invalidate(bounds())` 注掉。自测会在「a running tick does not owe the plot」红，窗口里波形也会停住，因为没有人再申报重画。你复原它，自测再绿。
- 把 `Knob::update_value` 里 `steps` 的方向反过来，东南西北四个定位判例会红（顺时针变减、逆时针变加）。再把中心 `dx == 0 && dy == 0` 的保护去掉，正中心判例会红，`direction_index(0,0)` 会把值带到 83。复原再绿。

我们把夜黑 XY、夜黑 YT、夜黑 SPEC、纸白 XY 四张 PBM 各转成 PNG，肉眼查过一遍。Xvfb 下 `--frames 120` 的真窗口冒烟退出码 0。真窗口的手感与关闭行为，照旧由作者在桌面上亲手走。

## 验收

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

验收脚本跑六道，我们逐道清点：

- 普通构建与全部测试
- ASan/UBSan
- TSan
- clang-format
- clang-tidy
- fontgen 独立构建

我们这一轮跑下来，六道全部通过。rel 道我们等了 133.89 秒，自测另加 0.01 秒。ASan/UBSan 是 134.74 秒加自测 0.05 秒，TSan 是 133.49 秒加自测 0.05 秒。格式、静态检查与 fontgen 两组全过。新增的 `visor.signal.selftest` 在普通、ASan、TSan 三道里都通过。你自己复核时，看脚本末行打印的 `acceptance: all green`。

## 下一批的口子

真窗口冒烟仍由作者亲手完成。之后是 X11 的提交策略（脏区矩形到 X 提交矩形的裁剪与合并），再往后才是更多示例。示例的条带重放成本也留在这里：示波区每帧要按条带重画 12 次，PC 上跑得动，但这一章还没有帧率判据。小 RAM 彩屏真要跑动画时，这个成本要么靠更粗的 profile，要么靠保留上一帧的局部缓冲，我们不假装它不存在。定点正弦的十行近似目前在旋钮和示例里各有一份。两份还抵不上收进 `base` 的代价，第三个消费者出现时我们再收拢。

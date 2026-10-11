# 第十一章 坐标窗口与条带重建

前十章的每一次绘制都默认一件事：缓冲就是屏幕。这一章把这个假设放弃掉。渲染核心从此只认「窗口」：本轮要更新哪块场景区域，缓冲就装哪块区域。整幅画面变成窗口的一种特例，水平条带是另一种，脏区重建是第三种。配套进场的是脏区收集器，一张固定容量的欠画清单，装不下就折叠成包围盒，保证只多画不漏画。整幅渲染与条带渲染的逐像素一致，由四道差分判例守着。

## 缓冲不等于屏幕

为什么非要放弃这个假设。桌面机器上整幅帧缓冲不算什么开销，320×240 的 1bpp 画面也才 9600 字节，可在目标硬件上这是要真金白银去买的 RAM：一块 8K 内存的 MCU 装不下整幅，装得下一条 320×8 的条带（320 字节）。规格把这写成硬边界：渲染核心不假设缓冲等于屏幕，屏幕尺寸只以「本轮要更新哪块区域」的身份进核心。

代价在绘制纪律上。条带化会把同一棵控件树画很多遍（每个条带一遍），所以控件绘制必须可重放：读状态、不改状态，不读事件、不推进动画。第九章立这条纪律时它还只是个测试断言（同一棵树画两遍逐字节一致），这一章它变成整个渲染模型的地基。

## 坐标窗口：画笔的第二副面孔

`CanvasPainter` 长出第二个构造参数。窗口是场景坐标系里的一个矩形，声明「这块区域归我画」：

```cpp
/// Whole-canvas window: the canvas is the entire scene, scene pixel (0, 0)
/// is the canvas corner.
explicit CanvasPainter(CanvasT& canvas) noexcept
    : canvas_{canvas},
      window_{.pos = base::PointI{0, 0},
              .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                  .h = static_cast<int>(CanvasT::kHeight)}} {}

/// Sub-window: the canvas stores exactly this scene rectangle, its pixel
/// (0, 0) holds the window's top-left corner. A window may be smaller than
/// the canvas (the tail strip of a rebuild); the surplus rows are left
/// untouched and never submitted.
CanvasPainter(CanvasT& canvas, base::RectI window) noexcept
    : canvas_{canvas}, window_{window} {}
```

单参构造是老朋友，窗口盖满整幅画布，行为与从前一字不差，已有调用一处不用改。双参构造是新面孔：控件的边界照旧写在场景坐标里，画笔负责把窗口内的部分平移到缓冲坐标（减去窗口原点），窗口外的部分裁掉：

```cpp
void fill(base::RectI area, canvas::Color ink) const noexcept override {
    const base::RectI visible = base::intersect(area, window_);
    if (visible.is_empty()) {
        return;
    }
    const draw::Pen<CanvasT, canvas::Color> pen{canvas_, ink};
    pen.fill(shift_to_buffer(visible));
}
```

裁剪做两道，各有各的职责。窗口裁剪（与 `window_` 求交）保证窗外的像素不进缓冲。第五章的画布拒写还在底下垫着，窗口比缓冲小的场合（尾条带只占四行，缓冲有八行）多余的行不会被碰到。`plot` 同一口径，窗外的点像画布越界一样无声消失。

测试把这个语义按在像素上：窗口 (5,5,10,10) 收到一整幅 On 填充后，缓冲 (0,0) 读亮，它是场景 (5,5)。缓冲 (9,9) 读亮，场景 (14,14)，半开区间的最后一个。缓冲 (10,10) 读暗，场景 (15,15)，窗外。尾窗口的判例单独一组：20×8 的缓冲收到 (0,16,20,4) 的窗口，填满整个场景后只有四行变亮，底下四行纹丝不动。

## 替身学会收区域

窗口渲染要有人接住。B1 的离屏替身只收整幅，别的区域一律拒绝计数。这一章它的收法放宽成界内即收：格式对、指针非空、矩形落在屏内且非空，就收。

```cpp
void OffscreenHost::handle_flush(const FlushRegion& region) noexcept {
    // The ch11 contract: any in-bounds region of the right format lands, not
    // just whole frames. A strip and a dirty rectangle are as welcome as the
    // full canvas; empty regions and out-of-bounds ones are refused and
    // counted, because both mean a sender that computed its window wrong.
    const bool acceptable = region.format == PixelFormat::Mono1 && region.pixels != nullptr &&
                            !region.rect.is_empty() && region.rect.pos.x >= 0 &&
                            region.rect.pos.y >= 0 &&
                            region.rect.pos.x + region.rect.size.w <= width_ &&
                            region.rect.pos.y + region.rect.size.h <= height_;
    if (!acceptable) {
        ++rejected_count_;
        return;
    }
```

拷贝从整行 `copy_n` 换成逐位搬运。原因在指针上：区域提交携带的像素指针指向区域自己的第一行，区域的 x 却可能落在任意位偏移上（脏区矩形不会替你八像素对齐），源与目标的位号对不上，只能一位一位搬：

```cpp
    // Bit-true copy one pixel at a time: region pixels may sit on any bit
    // offset, the frame packing never changes. The same pixel_bit_position the
    // canvas packs with unpacks here, the formula's one home paying off.
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    for (int row = 0; row < region.rect.size.h; ++row) {
        const std::uint8_t* src_row =
            region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
        const std::size_t dst_base =
            (static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) * stride;
        for (int col = 0; col < region.rect.size.w; ++col) {
            // ... read source bit, deposit into the frame byte ...
        }
    }
    ++flush_count_;
}
```

读位写位用的都是 `canvas::pixel_bit_position`。这个函数第一章定形时注释里写着「hosts decode with it too」，当时是句预告，这一章兑现。

核心侧同步长出区域提交入口。`submit_region` 是一般形，`submit_frame` 退化成它的特例（矩形取整幅画布），运行循环从此只走区域这一条路：

```cpp
[[nodiscard]] bool submit_region(const HostTable& host, base::RectI rect,
                                 const std::uint8_t* pixels, std::size_t stride_bytes) noexcept;
```

## 一个渲染原语，三种窗口

渲染收敛成一个原语：把整棵树在一个窗口下画进装着这个窗口的缓冲，然后把窗口提交出去。

```cpp
template <typename CanvasT>
void render_window(const Widget& root, CanvasT& buffer, base::RectI window,
                   const host::HostTable& host) noexcept {
    const CanvasPainter<CanvasT> painter{buffer, window};
    root.draw(painter);
    static_cast<void>(
        host::submit_region(host, window, buffer.pixels().data(), CanvasT::kStrideBytes));
}
```

整幅是它的特例（窗口盖满画布），`run_once` 的提交一行换成它，行为不变。条带是它的循环：

```cpp
template <typename CanvasT>
void render_strips(const Widget& root, CanvasT& strip, base::SizeI screen,
                   const host::HostTable& host) noexcept {
    const int strip_h = static_cast<int>(CanvasT::kHeight);
    for (int top = 0; top < screen.h; top += strip_h) {
        const int rows = (screen.h - top < strip_h) ? (screen.h - top) : strip_h;
        render_window(root, strip,
                      base::RectI{.pos = base::PointI{0, top},
                                  .size = base::SizeI{.w = screen.w, .h = rows}},
                      host);
    }
}
```

注意两边各是什么身份。条带缓冲是编译期的：`Canvas<SizeI{320, 8}>` 写在类型里，这是光栅 profile，预算在编译期确定下来。屏幕尺寸是运行期的：循环跑几圈、尾条带裁几行，到了才知道。同一个模型的两半，规格要的分工。

脏区版本只多一个问句：这条带欠不欠画？

```cpp
template <typename CanvasT>
void render_strips(const Widget& root, CanvasT& strip, base::SizeI screen,
                   const host::HostTable& host, const DirtyRegions& dirty) noexcept {
    // ... same loop ...
        if (dirty.touches(strip_window)) {
            render_window(root, strip, strip_window, host);
        }
}
```

收集器说没欠的条带，宿主上保留上一轮的像素。这三个函数合起来就是规格管线的中段：整幅、条带、脏区条带，一个原语三种窗口。

## 脏区收集：保覆盖，不保省

收集器是一张固定容量的矩形清单，唯一的不变量是覆盖：凡是被推进来的像素，必须还在某一条记录里。合并与折叠都围着这条转。

```cpp
void DirtyRegions::push(base::RectI area) noexcept {
    if (area.is_empty()) {
        return;
    }
    if (count_ == kCapacity) {
        // The full list folds into one box first, which frees the tail slots;
        // coverage of everything already here is the one promise that counts.
        collapse();
    }
    base::RectI grown = area;
    // Merge while the growing rectangle keeps touching residents. Every pass
    // retires one resident, so the walk terminates; the box only ever grows,
    // which can swallow empty space but never drops a covered pixel.
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (!base::intersect(areas_[i], grown).is_empty()) {
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

三条语义各管各的。空矩形不登记，它不欠任何像素。重叠的推入当场合并：新矩形与居民相交就并成包围盒，长胖之后继续找、继续并，直到没有交集，这叫级联，三个错开叠放的矩形进，一个包裹盒出。容量满了，先折叠：已有的全部记录折成一个包围盒占住第一格，新矩形自己占一格，于是列表回到两条，永远不会溢出。

读懂这条语义的关键是分清哪个是正确性、哪个是节俭。折叠是正确性，它保证清单装不下时什么都不会被忘掉。合并是节俭，它推迟折叠的到来。包围盒可以吞下大片没脏的区域（两个对角的矩形并成一个盒，中间干净的也算进去了），多画不扣分，漏画才是事故。`touches` 是渲染侧唯一的问句：任一记录与候选条带有交集就算欠。

包围盒本身进了 base：`bounding_box` 与 `intersect` 住在同一个头里，脏区收集是它的第一个用户，不会是最后一个。

## 判例：整幅与条带逐像素一致

条带化的全部风险收敛成一句话：同一棵树，整幅画一遍和分条带画三遍，像素必须一个不差。风险点数得出来。尾条带比别的短，会不会把上一条带的内容带进来。控件挪走后旧位置还亮着，说明该重画的条带没画。跨条带的形状，两次进入会不会画出两种结果。折叠坏掉的清单，会不会漏掉整条带。判例矩阵就按这个风险清单来。

四道场景共用同一个测试画面：20×20，全屏亮底，一块压着条带 0/1 边界（第 8 行）的暗色挖孔，两个藏在条带里的亮点。挖孔承担「重叠对象」这一道：它跨在条带边界上，先序绘制的次序错一次，孔就消失，比对当场翻红。四道场景各验一件事：

- 尾块：屏幕 20、条带 8，最后一个窗口只有四行。整幅提交一次，条带提交三次（8+8+4），提交次数本身就是断言。
- 旧位置恢复：亮点从尾条带挪到顶部。先让替身收下完整的上一帧（它扮演一块真的持有旧像素的屏幕），再走脏区路径只重画新旧位置所在的两条带，提交次数恰好 3+2，然后与整幅的第二帧逐像素比对。装错的条带在这里被逮住。
- 全屏失效：一次把整个屏幕推进收集器，三条带全重画。
- 溢出坍缩：九个互不相交的小矩形推进容量八的清单，前八个折成一盒、第九个自占一格，两格合起来跨满三条带，重画一个不少，像素照样逐点一致。

每个场景都做两层断言。像素层，整幅宿主与条带宿主的 400 个点逐位相同。次数层，`flush_count` 数着提交了几次，脏区路径省下哪几次当场可见。窗口裁剪与收集器本体另有单元判例把守，四道场景不必独自扛下一切。

## 验收

红绿实验做在折叠上，它是不变量的心脏。打开 `src/ui/dirty_regions.cpp`，把 `collapse()` 末尾的 `areas_[0] = box;` 改成 `areas_[0] = areas_[0];`，算出来的包围盒不要了，第一格原地不动。重建并跑测试，会看到两拨红：先是收集器单元判例，`has_region` 与 `touches` 一连串 FAIL，被折叠的记录丢了。接着是条带判例的溢出坍缩场景，跨满三条带的盒子缩成一个角，`flush_count` 对不上 3，像素比对给出第一失配坐标。复原成 `areas_[0] = box;`，全绿。

这条实验值得多看一眼：折叠退化成「丢掉历史」之后，正确性断言和节俭断言一起红。清单坏掉的方式有很多种，但漏画的后果只有一种，判例矩阵把它按在像素上。

## 参考

- [A15 起草指导][brief]：渲染核心与缓冲预算的规格原文。
- [第九章 控件树与运行循环][ch9]：可重放纪律的来历，`run_once` 的第一版。
- [第十章 焦点链与复选框][ch10]：宿主表分家，本章的替身住在分出去的那一半。
- [B4 批记][b4note]：条带批次的前置决定（先用 1bpp 证一致）。
- [B5 批记][b5note]：本章批次的验证结果与接口决定。

[brief]: ../../.claude/spec/aels-a15-gui-brief.md
[ch9]: 09-widget-tree.md
[ch10]: 10-focus-checkbox.md
[b4note]: ../../.claude/notes/2026-10-11-b4.md
[b5note]: ../../.claude/notes/2026-10-11-b5.md

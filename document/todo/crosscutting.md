# 交叉清单（不绑站的长期项）

这些工作不单独占验收梯队的站，按批次穿插推进。哪一批做哪项由当批规划定，完成划掉。

## 字体与文本

前身 Cinux-GUI 的 P5 地盘。全组已收尾（B7 至 B11，2026-10-12）：转换核心、图片线、字形渲染、文本测量与 Label、BDF 汉字读入全部落地，第二站门槛（取模结果显示中文/图片）的数据侧齐备。

- [x] fontgen 转换核心与图片线（B7，2026-10-12）：PBM 转画布同款打包的点阵数组头文件，独立构建、测试与 CLI 齐备。
- [x] fontgen 汉字线（B11，2026-10-12）：输入源 BDF（拍板时否决 HZK16，授权链不干净）。`fontgen bdf` 按码点范围切子集，DWIDTH 与 BBX 折算进 `GlyphEntry` 的 advance 与偏移（`offset_y = ascent - (yoff + height)`），整库导出有意拒绝。真 Unifont 烟雾过（ASCII 95 字与单汉字子集，生成头编译期消费）。
- [x] 字形渲染进渲染核心（B9，2026-10-12）：`MonoBitmap` 视图（生成头拿来即用）加 `Painter::draw_bitmap`（墨水传送语义，整数缩放位变块），条带重放安全有判例。
- [x] 文本测量与 Label 控件（B10，2026-10-12）：字模组头文件与 fontgen 字库模式（条带切割），UTF-8 解码进 base，`TextLayout` 游标统一测量与绘制的间距，`draw_text` 自由函数，`Label` 靠测量自己定尺寸（`Widget::set_bounds` 旧新双申报）。

## 控件与布局

- [ ] Button 的键盘激活：等节奏故事（按键重复或时钟节拍），B6 判例已按住「聚焦不激活」这个推迟行为，故事进场先翻那条判例。
- [ ] KeyUp 消费者与松开跟踪：键盘版的按压暂态。
- [ ] 手势批次：PointerMove 消费、按住滑出去松手的语义、点击合成的取舍，第九与第十二章留的口子。
- [ ] Slider、Container 与布局系统（HBox/VBox/flex）。Label 已随 B10 进场（自动尺寸，见字体与文本组）。
- [ ] TextBox、Radio、Dropdown：前身 P6/P7 半途停下的地方。
- [ ] 主题与圆角：前身 P3/P5 已有（含运行时主题切换与 per-corner 圆角），visor 未立项。

## 性能与优化（分叉前置第二条的优化半边）

- [ ] 区域提交的整字节快路径：同 x 对齐时按字节搬，逐位路径留作正确性底线（B5 开放项）。
- [ ] 渲染优化盘点批次：具体项等真板探针数据，不预先拍。

## 结构决定（拍板过的原则与挂起项）

- [ ] events.hpp 分文件之问：作者未拍板（B6 收尾的 CR 答疑），AI 建议维持一个文件，词表撑大再分。
- [ ] widget_tests 拆文件：B4 起挂着，判例再多就分。
- [ ] 构建保持普通 `cmake -S -B`，不用 presets 文件：从零定位的一部分，任何一站都适用。
- [x] Cinux 挂钩方式拍板（2026-10-12）：CinuxOS 侧构建脚本把 visor/include 挂进 `-I`（`VISOR_ROOT` 可配），不 submodule、不复制同步。Cinux 线同日缓行（作者定），B12 真接线的时机另定。
- [x] 标准库白名单按层守严（2026-10-12）：进 Cinux 链接的层（base、canvas、draw、ui 与 bindings/cinux）只用 header-only 词汇（span、string_view、optional、variant、array、expected、algorithm 一族），禁 string、vector、filesystem、fstream、format、print；offscreen 与 tests 放宽（PC 专属）。动机：CinuxOS 用户态是 musl libc.a 单枪匹马、无 libstdc++，前身核心零 STL 容器的纪律 visor 同样要守。清零动作（Label 文本改存 string_view、point_format 从 base 搬回 tests）随 Cinux 准备批落地，作者已拍板改法。

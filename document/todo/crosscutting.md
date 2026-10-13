# 交叉清单（不绑站的长期项）

这些工作不单独占验收梯队的站，按批次穿插推进。哪一批做哪项由当批规划定，完成划掉。

## 字体与文本

前身 Cinux-GUI 的 P5 地盘。全组已收尾（B7 至 B11，2026-10-12）：转换核心、图片线、字形渲染、文本测量与 Label、BDF 汉字读入全部落地，第二站门槛（取模结果显示中文/图片）的数据侧齐备。

- [x] fontgen 转换核心与图片线（B7，2026-10-12）：PBM 转画布同款打包的点阵数组头文件，独立构建、测试与 CLI 齐备。
- [x] fontgen 汉字线（B11，2026-10-12）：输入源 BDF（拍板时否决 HZK16，授权链不干净）。`fontgen bdf` 按码点范围切子集，DWIDTH 与 BBX 折算进 `GlyphEntry` 的 advance 与偏移（`offset_y = ascent - (yoff + height)`），整库导出有意拒绝。真 Unifont 烟雾过（ASCII 95 字与单汉字子集，生成头编译期消费）。
- [x] 字形渲染进渲染核心（B9，2026-10-12）：`MonoBitmap` 视图（生成头拿来即用）加 `Painter::draw_bitmap`（墨水传送语义，整数缩放位变块），条带重放安全有判例。
- [x] 文本测量与 Label 控件（B10，2026-10-12）：字模组头文件与 fontgen 字库模式（条带切割），UTF-8 解码进 base，`TextLayout` 游标统一测量与绘制的间距，`draw_text` 自由函数，`Label` 靠测量自己定尺寸（`Widget::set_bounds` 旧新双申报）。

## 控件与布局

- [x] Button 的键盘激活（B12，2026-10-12）：节奏故事拍板为「一轮一键」（双泵遇键即停、指针免费排空，按压帧由泵的结构担保，宿主重复与时钟两条路都有缺口），Enter/Space 压下、配对抬起完成激活，指针点击同路开火，回调为裸函数加上下文词。
- [x] KeyUp 消费者与松开跟踪（B12，2026-10-12）：`Widget::on_key_up` 沿 KeyDown 的投递路进焦点持有者，Button 靠它完成激活，Checkbox 刻意不受抬起影响（翻转即按压）。
- [x] 手势批次（B14，2026-10-13）：按压归属住 `InputRouter`（捕获语义，调用方持有、与 DirtyRegions 同款），移动有主归主（拖拽地基）、无主按命中（悬停地基），抬起归主并退役（卡死按压翻案），`on_pointer_up` 带位置（捕获下落点任意），点击合成为松手落回界内才开火（B12 的巧合升格明文）。
- [x] Slider 与布局系统 HBox/VBox（B15，2026-10-13）：`set_bounds` 提公开（B10 留给布局故事的位置兑现，申报照旧），`Widget::child(index)` 取用口，行盒列盒一趟走法（主轴保留、副轴拉伸、间距步进，重排是调用方的事、嵌套外先内后），Slider 站在捕获地基上（按下即抓取、拖拽两端钳制、无键盘腿等方向键进核心词表）。flex 比例分配一族未做，等真有界面要它再议。
- [x] TextBox（B16，2026-10-13）：单行编辑器，定容 64 字节缓冲（白名单纪律生来就守），可打印码点为字的 KeyDown 约定、编辑键进核心词表（方向键取带外值 0x110 一族，evdev 的 103-108 落在可打印区与文本约定正面相撞，词表注释写明裁决），窗口随插入点前后滑，set_text 比较先于拷贝（判例先红逮住的 bug）。
- [x] Radio、Dropdown（B18，2026-10-13）：RadioGroup 一个控件装下互斥（选项标签视图、定容八项、上下键钳制走、Enter/Space 刻意空），Dropdown 关闭一行打开长成列表（set_bounds 走布局自己的路、高亮反色、选中即收），方向键第一次有了消费者。附带 set_bounds 双申报的 CR 打磨（旧框命名局部，含义不靠赋值位置）。前身 P6/P7 半路的地方补完。
- [ ] 主题与圆角：前身 P3/P5 已有（含运行时主题切换与 per-corner 圆角），visor 未立项。

## 性能与优化（分叉前置第二条的优化半边）

- [x] 区域提交的整字节快路径（B13，2026-10-12）：离屏宿主的 `handle_flush` 在 `pos.x` 对 8 整除时按字节搬（整字节段直赋，尾位段按 `pixel_bit_position` 算出的活位掩码融合，源零填充不盖目标邻居位），逐位路径留作正确性底线。SSD1306 的 `write_region` 同为逐像素但不在本批：页布局的字节是纵向八行，快路径是另一种对齐故事，且驱动适配已移交专门线（方法论原样可用）。
- [ ] 渲染优化盘点批次：具体项等真板探针数据，不预先拍。

## 结构决定（拍板过的原则与挂起项）

- [ ] events.hpp 分文件之问：作者未拍板（B6 收尾的 CR 答疑），AI 建议维持一个文件，词表撑大再分。
- [x] widget_tests 拆文件（B12，2026-10-12）：tidy 的函数体量线逼的，控件域判例（Label、按钮激活、复选框抬起免疫）分进 control_tests，树与框架留在 widget_tests，替身画笔两边各持一份。
- [ ] 构建保持普通 `cmake -S -B`，不用 presets 文件：从零定位的一部分，任何一站都适用。
- [x] Cinux 挂钩方式拍板（2026-10-12）：CinuxOS 侧构建脚本把 visor/include 挂进 `-I`（`VISOR_ROOT` 可配），不 submodule、不复制同步。Cinux 线同日缓行（作者定），B12 真接线的时机另定。
- [x] 标准库白名单按层守严（2026-10-12）：进 Cinux 链接的层（base、canvas、draw、ui 与 bindings/cinux）只用 header-only 词汇（span、string_view、optional、variant、array、expected、algorithm 一族），禁 string、vector、filesystem、fstream、format、print；offscreen 与 tests 放宽（PC 专属）。动机：CinuxOS 用户态是 musl libc.a 单枪匹马、无 libstdc++，前身核心零 STL 容器的纪律 visor 同样要守。清零动作已落地（B17，2026-10-13：Label 改存 string_view、point_format 搬回 tests），Cinux 链接层零重库违规。

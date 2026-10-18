# visor 升级设计底稿：像素层策略与补件清单

> 起草日 2026-10-17 · 状态：待作者过目，随后作为上游提案底稿带到 visor 会话 · 依据：documents/research/ 九篇调研（对 visor HEAD `9294df8` 逐文件核对）加当日方向拍板

## 0. 这份文档是什么

给 visor 会话的上游提案底稿。vesper 遵守互不回搬纪律（交接文档 §8），一切 visor 侧改动走上游提案，不私改借壳源。本文汇总两样东西：2026-10-17 当日拍板的设计决定对 visor 提出的新要求，加调研缺件清单里已定倾向「回填 visor」的既有件，按提案时机分束，供作者过目定稿。

## 1. 当日拍板的设计决定（影响 visor 的三条）

### 1.1 像素格式是编译期策略

决定：渲染器按像素格式模板化，每个目标在编译期选定一种格式，运行时零转换、零分派。三档起步：Mono1、RGB565、XRGB8888。

理由：桌面彩色（远期 Wayland 服务端站要宿 32 位应用缓冲）与嵌入式（i.MX6ULL 面板多为 RGB565，真单片机场景 1bpp 最省内存）由同一个策略覆盖，不需要两套代码。与 visor 家风同构：无堆、定容、编译期常量治理一切。

### 1.2 开放生态梯子

决定：生态口走开放标准，分三级。第一级终端（PTY 加 VT 转义序列，开放标准），D4 通的那天整个 Unix 命令行生态即可用。第二级 X11 后端嵌外来窗（XReparentWindow，明写为 X11 特权）。第三级远期长成 Wayland 合成器（服务端侧），XWayland 让 X11 应用免费跟进。自有 App 口定位教学通道加 Cinux 通道，协议文档公开。

对 visor 的约束：base 词汇保持无操作系统环境也可用的形态，不污染裸机可能性。

### 1.3 嵌入式判例机

决定：i.MX6ULL（Cortex-A7，跑标准 Linux，fbdev 加 evdev 形状）当 RGB565 站的实弹判例机，交叉编译静态二进制部署。它不是单片机，但它的面板与算力约束代表了嵌入式一端。真单片机（STM32 加 LTDC 裸机）作为设计约束保留，不定站。

对 visor 的要求：判例集加一台交叉编译目标。

## 2. 像素层设计细则（提案束 D 的主体）

### 现状核实（HEAD `9294df8`，逐文件看过）

- 传输缝已对：`include/visor/bindings/host.hpp` 的 FlushRegion 带 `PixelFormat` 枚举（现单成员 Mono1）、`const uint8_t* pixels`、`stride_bytes`，宿主表天生格式无关。
- 画布侧是单色形状：`canvas/color.hpp` 的 Color 枚举只有 Off 与 On，`canvas/mono_canvas.hpp` 与 `draw/pen.hpp` 按单色实现。

结论：抽象层容纳正确，要动的缝就是画布与画笔这一层，传输接口不用改。

### 设计形状

- pixel_traits 模板：每格式一份特性表，含像素类型、位深、每行字节数计算、单像素读写、反白与混合语义、墨色映射。
- Canvas 与 Painter 对 traits 模板化，Mono1 成为第一个实例化，而非唯一形状。
- Color 从两值枚举推广为「当前墨色」概念：Mono1 档是 Off 加 On，RGB565 与 XRGB8888 档是全值空间，宽度由 traits 决定，控件接口不感知。
- MonoFont 与 MonoBitmap 不动：字形与图标源数据是 1bpp 位图，按当前墨色着色喷射，三档通用。这本身就是省内存的设计，嵌入式端直接受益。
- 转换工具矩阵：已有 1bpp 扩 XRGB8888（cinux 绑定的 FrameTarget），补 RGB565 双向、XRGB8888 收 32 位应用缓冲量化到 RGB565（Wayland 服务端站吃）。
- 枚举扩员：PixelFormat 加 RGB565 与 XRGB8888。

### 模板化的代价控制

控件与判例按「实例化哪几档」收口：判例默认编 Mono1 加 RGB565 两档，XRGB8888 由 PC 后端与 Wayland 服务端站编。避免全矩阵编译时间爆炸。

### 判例建议

- Mono1 档全部现有判例不动，回归即验收。
- RGB565 档新增：基元判例（填充、线条、圆角在彩色下的像素读写）、字形着色喷射判例、i.MX6ULL 交叉编译加板上实弹（QEMU 截屏断言亮像素的同款手法）。
- 转换判例：三档两两转换的往返断言。

## 3. visor 补件总清单（按提案束分）

条目编号沿用 `documents/research/vesper-gap-analysis.md`，刀级与详情见彼处第 4 节。

### 束 A：事件 ABI 簇（建议一刀提，最早动）

| 编号 | 件 | 要点 | 刀级 |
|---|---|---|---|
| 1.7 | 滚轮事件 | Event variant 加 PointerWheel 成员，X11 翻译层放行按钮 4 与 5（event_wire.cpp 现显式丢弃，注释自认预留此层） | 小刀 |
| 1.4 | 修饰键 | KeyDown 加修饰位。Cinux 线 WireKey 的 shift、ctrl、alt 三旗已有，translate_event 丢弃处补齐，X11 的 state 同步进事件 | 小刀偏中 |

理由：事件表一动全宿主翻译层跟着动，越早定越便宜。消费者（视口、Alt+Tab）可以后到。

### 束 B：树语义簇（D1 开工前）

| 编号 | 件 | 要点 | 刀级 |
|---|---|---|---|
| 1.1 | 弹层挂载 | 画在兄弟之上、不受父几何约束、点外部收回的浮层。菜单内容模型归 vesper，挂载点归 visor | 中刀 |
| 1.3 | 窗口焦点域 | find_focusable_after 加子树边界，Tab 不跨窗口窜 | 小刀 |
| 1.5 | Z 序重排 | raise 与 lower 的子序重排接口，hit_test 逆序命中已备 | 小刀 |

理由：D1 的 WindowFrame 马上要吃 Z 序与焦点，弹层是 D2 右键菜单的前置。

### 束 C：手势与容量簇（D2 前）

| 编号 | 件 | 要点 | 刀级 |
|---|---|---|---|
| 1.18 | 双击手势 | 五事件之上的手势层第一件，连带拍时间窗默认值与单击延迟取舍 | 小刀 |
| 1.19 | 容器子表容量 | 倾向嵌套容器（KDE Containment 判例），上游扩容为备选，D2 实测后再议 | 小刀偏中 |

### 束 D：像素层簇（RGB565 站立项时，即本文第 2 节）

| 编号 | 件 | 要点 | 刀级 |
|---|---|---|---|
| 新 | pixel_traits 模板化 | Canvas 与 Painter 按格式模板化，Mono1 为第一实例化 | 中刀起 |
| 新 | 枚举扩员 | PixelFormat 加 RGB565 与 XRGB8888 | 小刀 |
| 新 | 转换矩阵 | RGB565 双向、32 位缓冲量化，已有 1bpp 扩 XRGB8888 为先例 | 小刀 |
| 新 | 交叉编译判例 | i.MX6ULL 目标入判例集，当 RGB565 实弹靶场 | 中刀 |

### 束 E：后到件（消费者立项时）

| 编号 | 件 | 要点 | 刀级 |
|---|---|---|---|
| 2.2 | 滚动视口 | 依赖束 A 的滚轮，render_window 的窗口重放是现成种子 | 中刀 |
| 2.3 | 多行编辑 | TextBox 同族，定容无堆纪律延续，编辑器 App 立项时提 | 大刀 |
| 2.4 | 列表视图 | vesper 先建后回捐，形状由文件管理器与系统监视器两个消费者各投一票再定型 | 中刀 |

### 不回填的分界线（记给 visor 会话做背景）

剪贴板（1.2）、任务栏（1.8）、spawn（3.1）、poll 编排（3.3）、定时器（3.4）、文件封装（3.5）、环境变量（3.6）全在 vesper 侧。分界线一句话：输入事实与树语义归 visor，桌面语义与宿主接线归 vesper。

## 4. 提案节奏建议

1. 立刻可提：束 A，加束 D 的枚举扩员半步（PixelFormat 先加成员占名分，模板化随后），一次上游动员。
2. D1 开工前：束 B。
3. D2 开工前：束 C。
4. RGB565 站立项时：束 D 主体。
5. 束 E 跟随各自消费者。

## 5. 开放问题（作者过目时拍）

- 判例默认编档组合：建议 Mono1 加 RGB565，XRGB8888 由特定目标编。是否同意。
- 束 A 与枚举扩员是否合并为一次上游动员（上文建议合并）。
- Mono1 桌面宿彩色应用（Wayland 服务端站在 RGB565 之前来时）走 Floyd–Steinberg 抖动，还是整站等 RGB565 之后。
- 双击手势时间窗默认值（通行 500ms 上下，clock_ms 现成）与单击是否等双击窗过期才触发。

## 6. 来源

- documents/research/vesper-gap-analysis.md（35 条缺件清单，visor 现状逐文件核对）
- documents/research/kde-plasma-architecture.md（Containment 嵌套，束 C 的 1.19）
- documents/research/window-and-session-theory.md（Wayland 协议族，第 1.2 节梯子第三级）
- documents/research/desktop-infrastructure.md（文本栈与 XDG，背景）
- 2026-10-17 当日对话拍板（开放生态梯子、Wayland 服务端远期站、像素编译期策略、i.MX6ULL 判例机）

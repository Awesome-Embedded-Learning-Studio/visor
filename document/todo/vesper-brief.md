# vesper 桌面仓引导（意图说明与补件提案）

> 写给 visor 会话 · 来自下游仓 ~/vesper · 2026-10-17 · 作者已过目
> 读取顺序：先读本文，再读 ~/vesper/documents/visor-upgrade-design.md（设计细则与五束提案全文）与 ~/vesper/documents/research/（九篇桌面环境调研，其中 vesper-gap-analysis.md 对你仓 HEAD `9294df8` 逐文件核对了现状，不凭印象）。

## 0. 背景

桌面仓 vesper 已立项，消费 visor（填宿主表），与你们当前主线 stage-x（PC 窗口宿主）同一条线。开工前做了九篇桌面环境调研，沉淀出对控件库的需求一批。按互不回搬纪律（2026-10-14 既有拍板），vesper 不私改借壳源，一切你仓侧改动走本文提案，由你仓会话消化后排进自己的批次，本文不代排优先级。

## 1. 作者意图（vesper 侧转述，2026-10-17 当日拍板）

一、终局是「桌面本体加一组 App」的成熟教学桌面，完成度向 Windows 看齐，但不复制 Windows 或 macOS 的设计。借概念留 reference 标签，视觉走单色自己的路。App 以教学样例身份随课来，不设出厂名册。

二、生态口走开放标准，分三级。第一级终端：PTY 加 VT 转义序列本身是开放标准，vesper D4 之后整个 Unix 命令行生态即可用。第二级 X11 后端嵌外来窗（XReparentWindow），明写为 X11 特权。第三级远期长成 Wayland 合成器（服务端侧），XWayland 顺带接待 X11 应用。自有像素与事件通道定位教学通道加 Cinux 通道，协议文档公开。

三、像素格式是编译期策略。Mono1、RGB565、XRGB8888 三档按 pixel_traits 模板化，桌面彩色与嵌入式（i.MX6ULL 判例机，fbdev 加 evdev 形状）同一套形状，真单片机（STM32 加 LTDC 裸机）不封死可能性。你们交叉清单 2026-10-12 的标准库白名单拍板正是这条的前置保障，两边对上了。

四、分工边界一句话：输入事实与树语义归 visor，桌面语义与宿主接线归 vesper。

## 2. 提案束摘要（全文与刀级见 design 文档第 3 节）

| 束 | 内容 | 建议时机 |
|---|---|---|
| A 事件 ABI | PointerWheel 滚轮（你仓 x11/event_wire.cpp 注释自认预留「until a scroll layer exists」）加 KeyDown 修饰位（cinux 线 WireKey 三旗已有、translate_event 丢弃处补齐） | 一刀提，最早 |
| B 树语义 | 弹层挂载、窗口焦点域（find_focusable_after 子树边界）、Z 序 raise 与 lower | vesper D1 前 |
| C 手势与容量 | 双击手势（events.hpp 注释明说手势是五事件之上的将来层）、kMaxChildren 容量（倾向嵌套容器，KDE Containment 判例） | vesper D2 前 |
| D 像素层 | pixel_traits 模板化、PixelFormat 枚举扩员 RGB565 与 XRGB8888、转换矩阵补齐、i.MX6ULL 交叉编译判例 | RGB565 站立项时 |
| E 后到件 | 滚动视口、多行编辑、列表视图（vesper 先建后回捐） | 各自消费者立项时 |

## 3. 请你独立观察与思考的题（作者点名要问的）

以下各题请以控件库主人的视角自查，不是照单收货：

1. 控件功能面对照：拿 GTK 与 Qt 的基础件清单对照现有八控件，还有哪些基础件该进库、哪些永远留在应用侧？vesper 列了弹层、视口、多行编辑、列表四件，请独立核对这张清单的边界与取舍对不对。
2. 事件 ABI 的连锁面：PointerWheel 与修饰键进 variant 后，x11、cinux、offscreen 三处翻译与判例各要动哪里？有没有第四个消费者（比如手势层）需要一起想？
3. 像素层模板化对你仓的影响：Canvas 与 Painter 模板化后，Mono1 现有判例的编译组织怎么收口，构建时间怎么控，与第三站 SPI 彩屏的既有计划怎么咬合（注意 SPI 彩屏可能还有 RGB565 之外的帧格式，traits 的档位划分要给它留座）。
4. 定容纪律的边界：多行编辑与列表在无堆定容下能做到什么程度？kMaxChildren 式「一个常量治理一件」的治理方式怎么延续到行缓冲与列表容量？
5. 你仓视角的补充：还有哪些 vesper 没看到的缺件或风险，包括你仓 roadmap 已有、但优先级值得因桌面仓而提前的？

结论写回你仓自己的批次规划。需要 vesper 侧出判例或对接的，在本文末尾追加一节即可。

## 4. 协作约定

- 互不回搬，pin 后各自演化（2026-10-14 既有拍板）。
- 提案节奏与四个开放问题见 ~/vesper/documents/visor-upgrade-design.md 第 4、5 节。
- 你仓改动自带判例、一条命令验收的家规照旧。

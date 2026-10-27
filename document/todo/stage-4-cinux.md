# 第四站：Cinux 与桌面层

渲染链对接 Cinux 内核，桌面语义（前身 Cinux-GUI 的 P4 至 P6 主菜）在本仓控件框架上重建。

## 待办

- [x] Cinux 宿主的 visor 侧（B17，2026-10-13）：白名单清零（Label 改存 string_view、point_format 搬回 tests，Cinux 链接层零重库违规），bindings/cinux 纯逻辑（1bpp 到 XRGB8888 的帧展开、/dev/event0 线格式到事件词表的翻译，PC 判例全覆盖）。
- [ ] Cinux 宿主的适配层：在 CinuxOS 仓里 mmap /dev/fb0、读 /dev/event0、挂时钟，接上宿主表三口，链接走脚本引用（VISOR_ROOT 可配，2026-10-12 拍板），按那仓自己的规矩开工。
- [ ] 字体与文本先落地：桌面层画不了没有文本的界面。前置已清（B10 收尾，2026-10-12：字形渲染、文本测量与 Label 都在），汉字线（BDF 读入）不挡宿主开工，与桌面层并行推进即可。
- [ ] WM/窗口/终端语义重建：窗口管理、终端渲染，全部走本仓控件框架。
- [ ] 窗口 resize 与最小化最大化：前身 P6 半途的活，别漏。
- [ ] 终端 ANSI 颜色渲染：前身做到 256 色背景前景，本站颜色 profile 已由第三站备好。
- [ ] 终端 IO：PTY 起进程。宿主表的第四口 spawn 已落地（槽位、SpawnRequest 词汇、POSIX 填充与判据，2026-10-24），终端线要用时直接填表。

## 留档备忘

挂钩方式已拍板（2026-10-12）：CinuxOS 侧构建脚本把 visor/include 挂进 `-I`（`VISOR_ROOT` 可配）。标准库白名单按层守严、Label 文本改存 string_view 同日拍板，清零动作已随 B17 落地（2026-10-13），详见[交叉清单](crosscutting.md)的结构决定一节。

GPU 纹理合成与多进程 Surface 协议是前身仓的远期设想（它的 P8/P9，从未开工），visor 未立项，记在这里防遗忘，不进排期。

## 开放问题

- 桌面层与 PC 窗口宿主谁先谁后未定（见 [梯队外](stage-x-pc-window.md)），桌面语义在哪个宿主上先调是开放问题。

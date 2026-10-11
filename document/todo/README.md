# visor 待办（按站索引）

待办按验收梯队的站分文件，一站一份。梯队顺序是作者拍板的路线（PC 离屏 → 真单色 OLED → 真 SPI 彩屏 → Cinux），站与站之间的功能批次（字体、控件、布局）不单独占站，收在交叉清单里，随批次取用。完成的项在对应站文件里划掉，详情归批记。

| 站 | 文件 | 状态 |
|---|---|---|
| 第一站 PC 离屏 | [stage-1-pc-offscreen.md](stage-1-pc-offscreen.md) | 已完成（B1 至 B6，2026-10-11） |
| 第二站 真单色 OLED | [stage-2-mono-oled.md](stage-2-mono-oled.md) | 未开始 |
| 第三站 真 SPI 彩屏 | [stage-3-spi-color.md](stage-3-spi-color.md) | 未开始 |
| 第四站 Cinux 与桌面层 | [stage-4-cinux.md](stage-4-cinux.md) | 未开始 |
| 梯队外 PC 窗口宿主 | [stage-x-pc-window.md](stage-x-pc-window.md) | 未开始，站位待定 |
| 交叉清单（不绑站） | [crosscutting.md](crosscutting.md) | 长期推进 |

## 分叉与留档

分叉前置三条（作者 2026-10-11 拍板）：到达 Cinux-GUI 曾有高度、完成优化与基础控件、小 RAM 彩屏分块探针通过。三条全过后 vendor 分叉，分叉点留档（批记一篇加提交记录）。暂不打 tag，分叉时再留档。分叉后两边各自演化，互不回搬。

对应关系：第一条前置（曾有高度）看交叉清单的字体与控件两组，第二条前置（优化与基础控件）看交叉清单的优化半边加控件组，第三条前置（分块探针）看第三站。

## 参考

- [A15 起草指导][brief]：项目目标、路线与规范基线。
- [B1 至 B6 批记][notes]：各批的验证结果、接口决定与开放项。
- [跟跑教程][tutorials]：正文按批次推进，每章对应一批的现状。

[brief]: ../../.claude/spec/aels-a15-gui-brief.md
[notes]: ../../.claude/notes/
[tutorials]: ../tutorials/README.md

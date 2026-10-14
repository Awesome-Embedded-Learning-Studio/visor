# 交叉清单（不绑站的长期项）

这些工作不单独占验收梯队的站，按批次穿插推进。哪一批做哪项由当批规划定，完成划掉。已完成的组与条目直接清除（作者 2026-10-14 定），明细见教程各章与提交记录。

## 性能与优化（分叉前置第二条的优化半边）

- [ ] 渲染优化盘点批次：具体项等真板探针数据，不预先拍。

## 结构决定（拍板过的原则与挂起项）

- [ ] events.hpp 分文件之问：作者未拍板（B6 收尾的 CR 答疑），AI 建议维持一个文件，词表撑大再分。
- [x] widget_tests 拆文件（B12，2026-10-12）：tidy 的函数体量线逼的，控件域判例（Label、按钮激活、复选框抬起免疫）分进 control_tests，树与框架留在 widget_tests，替身画笔两边各持一份。
- [ ] 构建保持普通 `cmake -S -B`，不用 presets 文件：从零定位的一部分，任何一站都适用。
- [x] Cinux 挂钩方式拍板（2026-10-12）：CinuxOS 侧构建脚本把 visor/include 挂进 `-I`（`VISOR_ROOT` 可配），不 submodule、不复制同步。
- [x] 标准库白名单按层守严（2026-10-12）：进 Cinux 链接的层（base、canvas、draw、ui 与 bindings/cinux）只用 header-only 词汇（span、string_view、optional、variant、array、expected、algorithm 一族），禁 string、vector、filesystem、fstream、format、print；offscreen 与 tests 放宽（PC 专属）。动机：CinuxOS 用户态是 musl libc.a 单枪匹马、无 libstdc++，前身核心零 STL 容器的纪律 visor 同样要守。清零动作已落地（B17，2026-10-13：Label 改存 string_view、point_format 搬回 tests），Cinux 链接层零重库违规。

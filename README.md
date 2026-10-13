# visor

取模工具 + 跨宿主 GUI 框架。同一份 GUI 核心按构建配置运行于四类环境：裸机 MCU 加单色 OLED、裸机 MCU 加 SPI 彩屏、Cinux（作者的 x86-64 教学操作系统）、桌面宿主。语言 C++23，禁异常、禁 RTTI，全程零第三方依赖。配套的取模工具把汉字与图片转成点阵数组，最终工具界面用本框架自己写。

这是一个从零教学工程：不 fork 现成仓库当起点，正文就是框架从零搭起来的过程本身，配[中文跟跑教程](document/tutorials/01-skeleton.md)。我们按批次推进，每批测试全部通过才算完成，批与批的推进记录收在[教程目录](document/tutorials/README.md)。

## 适不适合你

visor 面向想看一个跨宿主 GUI 框架如何从零搭起来的读者。想直接拿一个能用的 GUI 库，成熟方案更多，visor 的价值在每一层为什么这样设计。读教程需要 C++ 基本语法与命令行经验，不需要 GUI 或嵌入式背景。

## 现在有什么

框架已经能在 PC 上离屏跑通完整一轮管线：事件进，状态变，像素出。

- 构建与验证骨架。CTest 统一跑测试，验收脚本把六道检查（普通构建、ASan/UBSan、TSan、格式、静态检查、取模工具独立构建）合成一条命令。
- 基础词汇库。`Point`/`Size`/`Rect` 几何模板按半开区间语义处理矩形，`BitRange`/`BitMask`/`Bitmap` 位操作库自 Cinux 移植，1bpp 位布局画布行主序、MSB 在前。
- 事件与时间。指针与键盘的按下、抬起、移动事件用 `std::variant` 承载，宿主表提供显示提交、事件读取、毫秒时钟三口，核心经判空包装拉取，空表不终止。
- 控件层。`Widget` 树先序绘制、逆序命中，焦点链随 Tab 导航，已有 `Rectangle`、`Button`、`Checkbox`、`Label`、`Slider`、`TextBox`、`RadioGroup`、`Dropdown` 八个控件加 `HBox`/`VBox` 排列盒，下拉框打开时经 set_bounds 长成列表、新旧两框都申报欠画，`Label` 靠文本测量自己定尺寸，盒子排子走 `set_bounds`（新旧两框都申报欠画）。核心点名 Tab、Enter、Space、Backspace 与四个方向键（方向键取带外值，避开与文本码点约定的正面相撞），其余键码归宿主与控件自行解释，产文本的宿主把可打印键按码点送入。运行循环一轮一键（按下与抬起永不同轮，按压帧由结构担保），Button 键盘与指针双路激活，抬起配对完成。指针交互有捕获语义：按压归属住 InputRouter，移动有主归主（拖拽）、无主按命中（悬停），点击合成为松手落回界内才开火。
- 位图与字形。`MonoBitmap` 视图吃 fontgen 生成的点阵头文件，`Painter::draw_bitmap` 以墨水传送语义绘制，整数缩放每位变块，条带重放下与整幅逐像素一致。
- 文本。UTF-8 解码住 `base`，`MonoFont` 视图组装字模组头文件（码点二分查表、共享位块、每字模自带 advance 与偏移），`TextLayout` 游标是间距规则唯一住处，`measure_text` 与 `draw_text` 同源，换行与缺字跳过都有判例。
- 渲染管线。绘制发生在场景坐标系的窗口里，缓冲形状由编译期条带 profile 决定，屏幕尺寸只是运行期循环参数。控件状态改变时当场申报欠画区域，`run_dirty` 一轮只重画被申报的条带，区域装不下时折叠成包围盒，保证不漏画。区域提交有整字节快路径：字节对齐时按字节搬，尾位掩码融合不殃及邻居，逐位路径留作正确性底线。
- 取模工具 `tools/fontgen` 独立构建：子命令 `image` 把二进制 PBM（P4）图片转成与画布同款打包的 C++ 点阵数组头文件，子命令 `font` 把一张条带图按格子切成字模组头文件（码点连续标号，字距烤进格子），子命令 `bdf` 从 BDF 点阵字体（文泉驿、GNU Unifont 一类授权干净的自由字体）按码点范围切子集，比例字模自带 advance 与偏移，独立测试与命令行界面齐备。
- 第二站已开工的纯逻辑半边（2026-10-12 拍板 SSD1306、I2C、128x64）：bindings 按宿主分目录（离屏替身与 SSD1306 各立门户），画布行主序到 GDDRAM 页主序的转换在 PC 侧先行验证，I2C 传输与初始化时序等主控板定了再动工。Cinux 宿主的 visor 侧已落地：1bpp 到 XRGB8888 的帧展开与 /dev/event0 事件翻译在 `bindings/cinux` 以 PC 判例先行验证，Cinux 链接层标准库白名单零违规（适配层在 CinuxOS 仓另批）。

正在路上的：RGB565 彩色光栅、更多控件与布局、桌面窗口宿主、真实 OLED 与 SPI 彩屏开发板（驱动适配另线推进）、Cinux 宿主（visor 侧已落地，适配层在 CinuxOS 仓推进）。路线与每一步的待办见[待办清单](document/todo/README.md)。

## 构建与验证

需要一个支持 C++23 的编译器（比如 GCC）、CMake 3.28 以上与 Ninja。clang-format、clang-tidy、Doxygen 是可选的开发工具：装了才有格式检查、静态检查与 API 文档三个目标，没装不影响 configure、构建与测试。工具版本不设卡，不同版本的检查结论可能略有出入，这是接受的漂移。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

测试全部通过时 ctest 汇总 `100% tests passed`。

每批收尾的必跑验收是一条命令，六道检查（普通构建、ASan/UBSan、TSan、格式、静态检查、取模工具独立构建）任何一步失败即停，单项怎么手动跑也见 [验收脚本 README](scripts/checks/README.md)：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

## 文档地图

- [跟跑教程](document/tutorials/01-skeleton.md)：从空仓库到单选组与下拉框的二十四章正文，入口导读与逐批推进记录在[教程目录](document/tutorials/README.md)。
- [待办清单](document/todo/README.md)：按验收梯队分站的路线图，含分叉前置条件与留档决定。
- [验收脚本 README](scripts/checks/README.md)：六道检查各自覆盖什么。

API 文档不入库，`visor.docs` 目标生成到 `build/api/`。

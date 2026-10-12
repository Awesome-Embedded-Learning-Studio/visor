# 第十七章 BDF 字模线

汉字线的输入源在第十三章就挂着「待拍板」。2026-10-12 拍板了：BDF。否决的另一边是 HZK16 一类 GB2312 点阵字库——版权链条说不清（字模出处连考证的人都答不上来，下游拿它做字体的项目公开提醒过风险），一个公开的从零教程工程不能教读者去下载授权说不清的文件。BDF 这边有正主可指：文泉驿点阵宋体（GPL 加字体嵌入例外）、GNU Unifont（OFL 与 GPLv2+ 双授权），读者能合法拿到，教程指路指得安心。格式本身还有 Adobe 的正式文档，教学上站得住。

这一批五件事：BDF 解析器、`Glyph` 长出度量字段、emit 泛化成两种模式共用一张表书写器、CLI 第三个子命令 `bdf`、运行时侧补比例字模的判例。字体与文本组至此全部收尾。

## BDF 长什么样

BDF 是行式的文本格式，一个字模一小节：

```
STARTFONT 2.1
FONT -test-fixed-medium-r-normal--12-120-75-75-c-60-iso10646-1
SIZE 12 75 75
FONTBOUNDINGBOX 6 12 0 -2
STARTPROPERTIES 2
FONT_ASCENT 10
FONT_DESCENT 2
ENDPROPERTIES
CHARS 5
STARTCHAR comma
ENCODING 44
SWIDTH 500 0
DWIDTH 6 0
BBX 2 3 1 -2
BITMAP
c0
c0
80
ENDCHAR
...
ENDFONT
```

解析器的形状跟 PBM 头文法那套同款：一个行游标从头走到尾，关键字领起每行，认得的折进字模，认不得的（`SWIDTH`、`VVECTOR` 这些）跳过。错误措辞点名肇事者——哪个字模的 BITMAP 行数跟 BBX 说的对不上、哪个字模到了 BITMAP 还没有 DWIDTH，报出来直接能定位。`ENCODING -1` 的无编码字模跳过不抱怨；两个字模抢同一个码点才是错误（生成头的前置是码点升序，解析器排完序亲自验这道）。字模表按码点排序输出，文件里的顺序不作数。

`FONT_ASCENT`/`FONT_DESCENT` 是必需项，缺了报错而不是猜。行网格（ ascent 加 descent）是折算的基准，拿 `FONTBOUNDINGBOX` 去猜会在降部字模上静悄悄地错——包围盒描述的是全体字模的包络，不是基线的位置。

## 度量折算：从基线系到行顶系

这批的知识主体是一次坐标折算。BDF 的原点在基线，y 轴向上：`BBX w h xoff yoff` 说的是字模位图的左下角在基线上方 `yoff` 像素、右方 `xoff` 像素。第十六章的 `GlyphEntry` 要的是另一套：`offset_y` 是位图顶边到行顶的距离，y 轴向下。折算公式一行：

```
offset_y = ascent - (yoff + height)
```

`yoff + height` 是位图顶边在基线上方的高度，`ascent` 减它，得到顶边在行顶下方的距离。拿上面那个逗号验一遍：`BBX 2 3 1 -2`，`yoff` 是负的（降部），顶边在基线上方 `-2 + 3 = 1` 像素，`offset_y = 10 - 1 = 9`——位图落在行内第 9、10 两行，吊在基线底下，正是逗号该在的位置。`offset_x` 直接取 `xoff`，`advance` 取 `DWIDTH`。

BITMAP 的十六进制行不用折：BDF 的字节也是 MSB 在前，跟画布打包同构，解析出来直通。

## Glyph 长出度量，两种模式共用一张表

第十六章的 `fontgen::Glyph` 只有码点和像素，条带切割的 advance 由 emit 按格宽统一填。这批把度量搬进 `Glyph` 本身：`advance`、`offset_x`、`offset_y` 三个字段，条带切割填零值（advance=格宽，缝烤在格子里），BDF 按上节折算各带各的。emit 随之泛化：kBits 位块和 kGlyphs 表的书写器只此一份，逐字模读自己的度量，条带模式和 BDF 模式只差头部的横幅与注释段。生成头的形状一个字没动——第十六章的判例原样全绿，这就是当初「为比例字体留位」的兑现。

## 子命令与子集

CLI 长出第三扇门：

```
fontgen bdf <font.bdf> <output.hpp> --range <first:last> [--name <id>]
```

`--range` 收 `0x20:0x7e` 或 `32:126`，两端都进得去，选中的码点落入生成头。整库导出有意拒绝——Unifont 一个文件六万多字模，嵌入式的用法从来是按用字清单裁，一个范围一个头。退出码分工不变：缺 `--range`、范围写反、给了 `--cell` 都是 2；范围选不中任何字模是文件与范围合不来，退 1。工具版本 0.4.0。

## 判例实录

三回被真相纠正，两回在夹具、一回在主树判例自己身上。

第一回，CRLF 夹具把 `\r` 排反了。判例想造「真实换行序列」的字体文件，循环里在每个 `\n` 后面补 `\r`，造出来的是 `\n\r`——真实世界的 CRLF 是 `\r` 在前。解析器按 `\n` 切行、行尾_trim `\r`，对真 CRLF 全对，对这份假夹具全线 `\r` 挡在行首。手工用最小样例跑一遍命令行就看清了：解析器没错，夹具错了。

第二回，CLI 判例搜 `.offset_y = 3,`，生成的形状其实是 `.offset_y = 3},`——花括号在逗号前头。搜索串按想当然写，没对照生成物。

第三回最值钱：主树的手工字模判例给 demo 字库添逗号字模，表项顺手接在最后一位，码点 0x2c 排在 0x4e2d 后面——「码点升序」的前置被自己踩破，二分查找当场找不着逗号。修法不是挪一个表项了事，是整个字模资产按真实生成头的形状重排（kBits 顺序、byte_offset 全跟着走），手写资产也守生成头的约定。

红绿探针两根。折算探针把 `offset_y = ascent - (yoff + height)` 改成忘减字模高，红四处：三个字模的度量判例加逐字全文判例，lhs/rhs 把 10 对 3 摆在明面上。advance 来源探针把表书写器里读 `glyph.advance` 改成读位图宽，只有 BDF 的逐字判例红，条带的逐字判例不红——条带的 advance 恰好等于格宽，同值把差异盖住了。这个巧合正是 BDF 判例存在的理由：条带判例守不住它守的东西。

真字体烟雾一道：从 Debian 镜像拉 GNU Unifont 16.0.04 的 BDF（六万三千多字模），ASCII 范围切出 95 字、`0x4e2d` 切出「中」，两份生成头拿到主树 include 路径下编译期消费——ASCII 半宽 8、汉字全宽 16、`measure_text` 的 `static_assert` 全部成立。工具树独立构建测不到消费侧，这道手工烟雾两头补齐。

## 验收

主树六道一条命令，工具判例含在其中：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

亲手切一份真字库（Unifont 从 unifoundry 或 Debian 镜像可取，授权 OFL 与 GPLv2+ 双轨）：

```console
$ ./build-fontgen/fontgen bdf unifont_sample.bdf zhong.hpp --range 0x4e2d:0x4e2d
fontgen: 1 glyphs of line height 16, 32 bytes -> zhong.hpp (visor::assets::unifont_sample)
$ grep 'codepoint = 0x4e2d' zhong.hpp
    {.codepoint = 0x4e2d, .width = 16, .height = 16, .byte_offset = 0, .advance = 16, .offset_x = 0, .offset_y = 0}, // U+4E2D
```

红绿实验两个。第一个，把 `bdf.cpp` 里 `offset_y` 的折算改成 `ascent - (yoff + height - height)`（忘减字模高），跑 `fontgen.units` 看四个判例红、报文给出 10 对 3，复原再绿。第二个，把 `emit.cpp` 表书写器的 `glyph.advance` 换成 `glyph.image.width`，看 BDF 逐字判例红而条带判例不红——同值巧合盖住差异的一课，复原再绿。

## 下一批的口子

字体与文本组全部收尾，Cinux 线缓行中。交叉清单上现成的候选有两组：控件与布局（Button 的键盘激活等节奏故事、手势批次、Slider/Container 与布局系统），或者性能优化组的整字节快路径。选哪组开工，等作者定。

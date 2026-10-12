# 第十四章 SSD1306 的页布局

屏定了：SSD1306、I2C、128x64。第二站的门开了，可这一章一块板子都不碰。驱动里真正需要真板才能验证的是 I2C 传输和初始化时序，而有一块是纯逻辑，PC 上就能写就能测，屏的显存长什么样、画布的像素怎么搬进去。这块先行，等板子到手，剩下的是接线。

动身之前先办一件结构上的事：bindings 按宿主分目录。这是第十章评审时拍板的原则，本站执行第一个真实落点。

## bindings 分目录

第十章把宿主表契约与 PC 替身分过家，但目录还平铺着，`src/bindings/` 里 `host.cpp` 和 `offscreen_host.cpp` 挤在一起。分目录的切法顺着那次的逻辑：

```
include/visor/bindings/
  host.hpp            宿主表契约与判空包装
  events.hpp          事件词表
  offscreen/
    offscreen_host.hpp
  ssd1306/
    page_buffer.hpp
src/bindings/
  host.cpp            判空包装的实现，契约侧
  offscreen/
    offscreen_host.cpp
```

`host.hpp` 与 `events.hpp` 是契约不是宿主，留在顶层。每个宿主实现住自己的叶子目录，离屏替身第一个归位，SSD1306 的位置同时立起来。CMake 跟着分家：`visor.host` 只剩契约实现，`visor.host.offscreen` 装替身、链接前者。往后每个真宿主一条目录一个库，谁也不挤谁。

## 两种内存，处处相反

SSD1306 的显存（datasheet 里叫 GDDRAM）是页主序：128x64 的屏分成 8 页，每页 128 列，每列一个字节管 8 行，字节内最低位是这一页最上面的那行。跟画布的打包放在一起看，两个轴全反着：

| | 画布 | GDDRAM |
| --- | --- | --- |
| 组织 | 行主序，一行一行排 | 页主序，一页一页排 |
| 一个字节管什么 | 一行的 8 个像素（横向） | 一列的 8 行像素（纵向） |
| 位序 | MSB 在前，第一个像素占最高位 | LSB 在上，最上面的行占最低位 |

所以画布到显存没有 memcpy 可走，是实打实的翻译。第一颗像素就能看清：画布上 `(0,0)` 这颗点亮着的像素，在画布行里是 `0x80`（行首字节的最高位），进了 GDDRAM 是第 0 页第 0 列的 `0x01`（列字节的最低位）。翻译的判例就从这一颗写起。

为什么要费这个劲学屏的布局？因为 I2C 线上跑的数据单位是字节，SSD1306 的地址指针按页和列走，一次连续写透一整段列。线的语言是 GDDRAM 的，不是画布的，宿主得会翻译。

## PageBuffer：屏的显存替身

```cpp
class PageBuffer {
  public:
    static constexpr std::size_t kWidth = 128;
    static constexpr std::size_t kHeight = 64;
    static constexpr std::size_t kPages = kHeight / 8;
    static constexpr std::size_t kBytes = kWidth * kPages;

    static constexpr std::size_t byte_index(std::size_t page, std::size_t column) {
        return (page * kWidth) + column;
    }
    ...
};
```

1024 字节的 `std::array`，页挨着页。`byte_index` 是这个布局的唯一地址公式，跟画布的 `pixel_bit_position` 同一个立场，公式只此一家。像素读写走 `set_pixel`/`get_pixel`，越界静默跳过，与画布 `set`/`get` 同一个姿态：宿主会收到任意矩形提交，落在面板外的部分当作不存在，不报错不越界。

位操作照旧骑 `base::bit` 的词汇，`bit_at(row % 8)` 给出一位的 BitRange，`deposit` 写进去。手写移位在这里没有豁免理由，这不是 fontgen 那个独立工程，是主仓代码。

## write_region：翻译本身

```cpp
constexpr void write_region(PageBuffer& buffer, const FlushRegion& region) noexcept {
    for (int row = 0; row < region.rect.size.h; ++row) {
        const int screen_y = region.rect.pos.y + row;
        if (screen_y < 0 || screen_y >= static_cast<int>(PageBuffer::kHeight)) {
            continue;
        }
        const auto source_row = static_cast<std::size_t>(row) * region.stride_bytes;
        for (int col = 0; col < region.rect.size.w; ++col) {
            const auto source_col = static_cast<std::size_t>(col);
            const base::bit::BitMask<std::uint8_t> word{
                region.pixels[source_row + (source_col / 8)]};
            const bool on = word.has(base::bit::mask_bit<std::uint8_t>(
                canvas::pixel_bit_position(source_col)));
            buffer.set_pixel(region.rect.pos.x + col, screen_y, on);
        }
    }
}
```

吃的是 `FlushRegion`，宿主表 flush 回调收到的就是这形状，rect 半开、pixels 指向顶行首字节、stride 是行跳。源侧读位用画布的公式，目标侧写位用面板的公式，两端各自的布局各命名一次，翻译函数自己不发明任何位算术。

两条语义是设计进去的，判例守着。一是灭像素也要写：`on` 是 false 时清位不是跳过。条带模型会把同一对象画多次，控件挪走后旧位置靠背景重画恢复，恢复就是写灭。二是字节跨八行而区域高度可能不足八：区域只拥有它盖住的那些行，字节里其余的位是邻居的，一律不动。第十一章「奇数位区域提交证逐位搬运在字节中间落对位置」的那条判例，在这里换了轴再出现一次。

## 判例与探针

判例七条。原点像素一条（`0x80` 进 `0x01`，试金石）。8x8 对角线一条（源行从 `0x80` 走到 `0x01`，目标列字节从 `0x01` 走到 `0x80`，位的走向整个翻过来）。竖线跨页一条（y=7 是第 0 页的最高位，y=8 是第 1 页的最低位，隔一颗像素隔一页）。预填 `0xAA` 的缓冲上写 y=5..20 的横带一条（三页三种结果 `0xEA`、`0xFF`、`0xBF`，不动位存活直接看得见）。单行高区域一条、越界裁剪一条、读写往返一条。

判例红过两回，都是期望自己写错被真相纠正，都值得记。第一回：预填 `0xFF` 的缓冲上一条单行高的区域，我断言写完的列字节是 `0x01`——把字节当像素了。区域只有一行高，它只拥有每个列字节的最低位，其余七行的亮邻居原封不动，x=5 那颗写灭后字节是 `0xFE`。第二回：想构造「两颗亮一颗灭」的源行，写了个 `0xE0`，前三位全是 1，要的是 `0xC0`。两回都是判例比脑子诚实。

红绿探针两根。把面板位公式颠倒（`row % 8` 改 `7 - row % 8`，模拟把屏当 MSB-first），十八处断言红——位公式是全局性的，牵连是应该的。把 write_region 改成只置不清（`on` 才写），精确红一条，清位判例咬住的那条。

## 验收

```console
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
$ cmake --build build
$ ctest --test-dir build --output-on-failure
1/1 Test #1: visor.tests ......................   Passed
100% tests passed
```

红绿实验两个。第一个，把 `page_buffer.hpp` 里 `set_pixel` 的 `bit_at(static_cast<std::uint8_t>(row % 8))` 改成 `bit_at(static_cast<std::uint8_t>(7 - row % 8))`，重跑测试看十八处红在对角线与跨页判例上，复原再绿。第二个，把 `write_region` 末行改成 `if (on)` 才调 `set_pixel`，看清位判例红在 `0xFE` 那条断言上，复原再绿。

板上还欠的两块是 I2C 传输层与初始化时序，主控板定了才动工。到时 `PageBuffer::bytes()` 就是数据段的直接来源，这一章的翻译已经把它备好了。

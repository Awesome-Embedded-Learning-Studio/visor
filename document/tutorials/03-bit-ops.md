# 第三章 位操作库：从 Cinux 移植，顺手修掉两个真问题

第二章把坐标世界安顿好了，这一章往字节里走。单色画布的每一像素只占一个比特，像素与比特之间的换算、寄存器风格的位段读写、一大串比特的状态管理，都靠一套位操作词汇。这套词汇 visor 没有从零写，是从同一作者的另一个教学工程 Cinux（一台从零搭的 x86-64 教学操作系统）的 `base/include/cinux/bit_ops` 移植来的，移植日期 2026-10-07，命名空间与命名按 visor 规则改写。移植本身就是好教材：两个真问题在这个过程中暴露，都被修掉了。这一章结束时 `bit` 全部用例通过，下一章的画布将整套搬用这里的词汇。

库分三份：`bit_ops.hpp` 是散装的分片函数，`bitmask.hpp` 是带名字的位段词汇，`bitmap.hpp` 管任意多位的状态。全部住在 `include/visor/base/bit_ops/` 下，命名空间 `visor::base::bit`。

## 散装分片：半个字节、一个字节

`bit_ops.hpp` 提供五个小函数，半字节（nibble，4 个比特）与字节（byte，8 个比特）的取片：

```cpp
/// @brief Low nibble of any integer: the least significant 4 bits.
template <typename T>
constexpr T low_nibble(T value) {
    return value & 0xF;
}

/// @brief High nibble of any integer: bits 7..4.
template <typename T>
constexpr T high_nibble(T value) {
    return (value >> 4) & 0xF;
}

/// @brief The nibble starting at a given bit position.
template <typename T>
constexpr T nibble_at(T value, std::uint8_t shift) {
    return (value >> shift) & 0xF;
}
```

`low_byte`、`high_byte` 同款，不贴了。测试一望而知：

```cpp
    // Slice helpers.
    VISOR_CHECK_EQ(visor::base::bit::low_nibble(0xB6), 6);
    VISOR_CHECK_EQ(visor::base::bit::high_nibble(0xB6), 0xB);
    VISOR_CHECK_EQ(visor::base::bit::low_byte(0x1234), 0x34);
    VISOR_CHECK_EQ(visor::base::bit::high_byte(0x1234), 0x12);
    VISOR_CHECK_EQ(visor::base::bit::nibble_at(0x5678, 8), 6);
```

## BitRange 与 BitMask：位置是唯一手写的东西

寄存器和像素字节里到处是“第几位到第几位是一个字段”的知识。`bitmask.hpp` 开头的注释把整个库的哲学说完了，后半句我们当正文读：

> every mask value is derived from a position, so the position-to-value translation never happens inside a human head.

手写的知识只有位号。掩码值一律从位号推导出来，“第 5 位是 0x20”这类换算永远不在任何人的脑子里发生。人脑做这种换算必错，只是迟早。

字段的位置用 `BitRange` 说，最低位加宽度：

```cpp
struct BitRange {
    std::uint8_t low;
    std::uint8_t width;
};
```

带这份词汇的字是 `BitMask<T>`，`T` 是字的原始类型：

```cpp
template <typename T>
struct BitMask {
    static_assert(T(-1) > T(0));

    T raw = 0;

    constexpr BitMask() = default;
    constexpr explicit BitMask(T value) : raw{value} {}
```

`static_assert(T(-1) > T(0))` 在编译期确认 `T` 无符号：无符号类型的 `-1` 是全一的最大值，大于零，有符号类型的 `-1` 是负一，小于零。想拿 `int` 当字，编译期就被挡下。字宽本身因此成了类型系统里的事实，注释里那句话也是真的：一个 32 位寄存器的标志位没法按位或进 64 位的页表项里去，类型不同，编译不过。

### operator| 在 8 位字上的收窄

`operator|` 在 visor 里长这样：

```cpp
    constexpr BitMask operator|(BitMask other) const {
        return BitMask{static_cast<T>(raw | other.raw)};
    }
```

那圈 `static_cast<T>` 是移植时被编译器逼出来的。Cinux 原版写的是 `BitMask{raw | other.raw}`，在 64 位字上相安无事。换到 8 位字（下一章画布的每个字节都是一个 8 位字）就出事：C++ 的整数提升规则规定，比 `int` 窄的整型在做算术前先提升成 `int`，于是 `raw | other.raw` 的结果是 `int`，再往 `BitMask<uint8_t>` 的花括号初始化里放，从 `int` 到 `uint8_t` 是收窄转换，花括号初始化对收窄零容忍，编译直接红。修法就是显式转回 `T`。这类错误在 64 位字上永远潜伏，只在字宽变窄那天露头，移植恰好是字宽变化的那天。

### 读字段与写字段

`extract` 与 `deposit` 是这对词汇的主动词：

```cpp
    [[nodiscard]] constexpr T extract(BitRange range) const {
        return static_cast<T>((raw >> range.low) & field_mask(range.width));
    }

    constexpr void deposit(BitRange range, T value) {
        const auto placed = static_cast<T>(field_mask(range.width) << range.low);
        raw = static_cast<T>((raw & ~placed) | ((value << range.low) & placed));
    }
```

`extract` 右移到低位再掩宽度个一，读出右对齐的字段值。`deposit` 反着来，先清空那一跨度，再把值安放进去，范围之外的位原样保留。测试用 `0xB6` 走了一遍全程：

```cpp
    // extract reads a field right-justified, deposit rewrites only that span.
    const BitRange field{.low = 4, .width = 3};
    Mask8 reg{0xB6}; // 1011_0110: field bits 6..4 hold 011, low nibble is 6
    VISOR_CHECK_EQ(reg.extract(field), std::uint8_t{3});
    reg.deposit(field, 5);
    VISOR_CHECK_EQ(reg.raw, std::uint8_t{0xD6});
    VISOR_CHECK_EQ(reg.extract(field), std::uint8_t{5});
    VISOR_CHECK_EQ(reg.extract((BitRange{.low = 0, .width = 4})), std::uint8_t{6}); // untouched
```

`0xB6` 是二进制 `1011_0110`，我们跟着测试把它走一遍。`Mask8` 是测试开头起的 `BitMask<std::uint8_t>` 别名，就是 8 位字。位 6..4 那三个比特是 `011`，`extract` 读出 3。`deposit` 写进 5 之后整个字节变成 `1101_0110`，即 `0xD6`，低位半字节那 4 个比特始终没动，读出来还是 6。

单个比特也有速记。`mask_bit<T>(position)` 是只亮一位的掩码，`bit_at(position)` 是盖住那一位的宽度为一的 `BitRange`，`ones<T>(width)` 是右对齐的一串一：

```cpp
template <typename T>
constexpr T ones(std::uint8_t width) {
    if (width >= sizeof(T) * 8) {
        return static_cast<T>(~T{0});
    }
    return static_cast<T>((T{1} << width) - T{1});
}
```

开头那个饱和分支挡的是未定义行为：移位数达到字宽本身（`uint32_t` 上的 `1 << 32`）是未定义行为，饱和分支先把这条路截走，要全一的字直接给全一。测试连着验证：

```cpp
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint8_t>(3), std::uint8_t{0x07});
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint8_t>(8), std::uint8_t{0xFF});
    VISOR_CHECK_EQ(visor::base::bit::ones<std::uint64_t>(64), ~std::uint64_t{0});
```

## Bitmap：给任意多位管状态

`bitmap.hpp` 处理“很多个比特”的场景，比如分配器要标记整页内存的占用情况。设计前提写在文件头：存储归调用者，`base` 层保持零分配。

```cpp
/** @brief Sentinel returned when no run of the wanted length exists. */
inline constexpr std::uint64_t kInvalidIndex = ~std::uint64_t{0};

/** @brief Storage word type of every bitmap. */
using WordMask = BitMask<std::uint64_t>;
```

### unsigned long 在 32 位平台上说谎

`WordMask` 用 `std::uint64_t` 而不是 `unsigned long`，这不是审美。文件头注释记着原委：Cinux 的 `unsigned long` 在开发用的 PC 上是 64 位，到了 ARM 目标上是 32 位，而这个库里 `index / 64` 的字算术假设每个字都是 64 位。数据模型（data model，各基本类型宽度的平台约定）不同，同一段代码换了平台就悄悄算错。你对着算一下就明白：字还是被当成 64 位去跳，实际每个字只装得下 32 个比特，一半的比特没人管。命名类型 `std::uint64_t` 把宽度写进名字，在每个宿主上都是同一个宽度。visor 侧修掉了，Cinux 侧原文保留，作为给原仓库的隐患提醒。

### 容量按比特计，尾部不越界

```cpp
    static constexpr std::uint64_t words_for(std::uint64_t bit_capacity) {
        return (bit_capacity + 63) / 64;
    }
```

容量按比特数说，需要几个字由 `words_for` 推导。100 个比特要两个字。`init` 把调用者的字数组绑进来并全部清零：

```cpp
    std::array<WordMask, Bitmap::words_for(100)> storage{};
    Bitmap map;
    map.init(storage.data(), 100);
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{0});
```

置位与清位都经由 `BitMask` 的词汇，计数永远诚实：

```cpp
    void set(std::uint64_t index) {
        WordMask& word = words_[index / 64];
        if (!word.has(mask_bit<std::uint64_t>(static_cast<std::uint8_t>(index % 64)))) {
            word.deposit(bit_at(static_cast<std::uint8_t>(index % 64)), 1);
            ++count_set_;
        }
    }
```

重复置位不涨计数，重复清位不掉计数。测试专门验了这条：

```cpp
    map.set(0);
    map.set(63);
    map.set(64);
    map.set(99);
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{4});
    VISOR_CHECK(map.test(0));
    VISOR_CHECK(map.test(63));
    VISOR_CHECK(map.test(99));
    VISOR_CHECK(!map.test(1));
    map.set(63); // already set, the count stays honest
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{4});
    map.clear(63);
    VISOR_CHECK(!map.test(63));
    VISOR_CHECK_EQ(map.set_count(), std::uint64_t{3});
```

`set_all` 灌满时对尾字做掩码，容量外的比特永远不假装存在：

```cpp
    void set_all() {
        if (capacity_ == 0) {
            return;
        }
        const std::uint64_t words = words_for(capacity_);
        const auto tail_bits = static_cast<std::uint8_t>(capacity_ % 64);
        for (std::uint64_t word_index = 0; word_index < words; ++word_index) {
            words_[word_index] = WordMask{ones<std::uint64_t>(64)};
        }
        if (tail_bits != 0) {
            words_[words - 1] = WordMask{ones<std::uint64_t>(tail_bits)};
        }
        count_set_ = capacity_;
    }
```

我们数一遍：100 容量灌满，第二个字的尾 28 位（比特 100..127）保持零，`set_count()` 恰好 100。要是尾字也被灌成全一，计数就虚了 28。

### find_clear_run：跨字边界的找空当

```cpp
    [[nodiscard]] std::uint64_t find_clear_run(std::uint64_t length) const {
        if (length == 0 || length > capacity_) {
            return kInvalidIndex;
        }
        std::uint64_t run = 0;
        const std::uint64_t words = words_for(capacity_);
        for (std::uint64_t word_index = 0; word_index < words; ++word_index) {
            if (words_[word_index].raw == ones<std::uint64_t>(64)) {
                run = 0;
                continue;
            }
            for (std::uint8_t bit = 0; bit < 64; ++bit) {
                const std::uint64_t bit_index = (word_index * 64) + bit;
                if (bit_index >= capacity_) {
                    break;
                }
                if (test(bit_index)) {
                    run = 0;
                    continue;
                }
                if (++run == length) {
                    return (bit_index + 1) - length;
                }
            }
        }
        return kInvalidIndex;
    }
```

外层先看整字：全一的字整字跳过，一个清比特都没有，不值得逐位走。内层逐比特找连续清位，`run` 跨字累积，所以连续空当可以横跨两个字。找到长度即返回起点，走到底没有就交哨兵 `kInvalidIndex`。

跨字那条用例我们亲手推一遍。容量 100，先 `clear_all` 再 `set_range(0, 63)`，点亮比特 0..62：

```cpp
    // A run may straddle the word boundary: bits 0..62 set, 63 and 64 clear.
    map.clear_all();
    map.set_range(0, 63);
    VISOR_CHECK_EQ(map.find_clear_run(2), std::uint64_t{63});
```

第一个字的比特 63 与第二个字的比特 64 都清着，连续两个清比特从 63 开始，起点恰好落在两个字的接缝上。若实现里 `run` 在换字时错误清零，这条就红。

## 这套词汇去哪

下一章的画布是第一个大用户。画布的每个字节是一个 8 位的 `BitMask` 字，每个像素是 `bit_at(pixel_bit_position(x))` 处的一比特字段，位置到值的换算全库只有一处。你现在已经把这份位词汇备齐了。

## 验收

```bash
cmake --build build/rel
ctest --test-dir build/rel --output-on-failure
```

`bit` 全部用例通过，与 `point`、`rect` 一起在 `visor.tests` 里过。

再做一次跨字实验。编辑 `tests/bit_tests.cpp`，把跨字用例的置位范围从 63 改成 64：

```cpp
    map.set_range(0, 64);
```

先别跑，自己推一遍：这一次比特 0..63 全亮，接缝上第 63 位也占了，第一个长度为 2 的连续清位从哪里开始？把你的答案填进断言的期望值，跑 `ctest --test-dir build/rel --output-on-failure` 验证。推对了你会看到绿，推错了 FAIL 行会指明这条断言，表达式原文都印在那里，你再回头推第二遍。实验完把 63 改回去。

下一章正式开机：画布，1bpp 的位布局、奇数宽度的填充位、还有一个叫 `Color` 的名字为什么这样定。

## 参考

- [B1 批记（2026-10-07）：移植与两处修复的记录][note]
- [第二章 几何词汇表][prev]
- [第四章 画布][next]

[note]: ../../.claude/notes/2026-10-07-b1.md
[prev]: 02-geometry.md
[next]: 04-canvas.md

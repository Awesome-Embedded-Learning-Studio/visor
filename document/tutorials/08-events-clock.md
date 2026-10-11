# 第八章 事件与时钟：宿主表长出另外两项

第七章是第一批次（B1）的关卡。从这一章起进入第二批次（B2）：宿主表不再只有显示提交一项，事件读取与毫秒时钟两项跟着长出来。核心从此听得见按键、看得到指针、摸得到时间，但边界没变，所有对话仍然只过那一张表。这一章结束时验收从五道变六道（TSan 进场），你还能在回绕的时钟上亲手做一次红绿实验。

代码注释里的章号按规格章节表计数，Ch1 对应 B1，本章对应的 ch2 就是 B2，与本书的章号不是一套。

## 事件：五个词的词汇表

事件词汇住在 `include/visor/bindings/events.hpp`，命名空间 `visor::host`。一共五个：

```cpp
/// @brief A key went down. code is host-defined: a PC host maps its platform's
///        key symbols, a board host maps its matrix scan lines. The core only
///        ever compares codes, it never decodes them.
struct KeyDown {
    std::uint32_t code;
};

/// @brief A key came back up, closing the KeyDown that carried the same code.
struct KeyUp {
    std::uint32_t code;
};

/// @brief The pointer moved. pos is in canvas coordinates.
struct PointerMove {
    base::PointI pos;
};

/// @brief A pointer button went down at pos. button is host-defined and small.
struct PointerDown {
    std::uint32_t button;
    base::PointI pos;
};

/// @brief A pointer button came back up at pos, closing the PointerDown that
///        carried the same button.
struct PointerUp {
    std::uint32_t button;
    base::PointI pos;
};

using Event = std::variant<KeyDown, KeyUp, PointerMove, PointerDown, PointerUp>;
```

五个类型，五个最小的自述：键按下、键抬起、指针移动、指针按下、指针抬起。手势、文本输入、按键重复都是后面的层，从这五个上长出来，不加新表项。

载体选了 `std::variant`（类型安全的联合体：同一时刻装着五个类型之一，读的时候声明你要哪个，拿错了在运行期报错而不是悄悄错位）。选它的理由是类型安全：`std::get<KeyDown>(event)` 拿不到就是明明白白的错误，裸联合体加种类枚举的写法少一层保护。五个成员都是平凡类型（可按字节复制、无构造析构负担），所以 `Event` 自己也是平凡的，这条不是口头信任，文件里跟着一行静态断言：

```cpp
// The ch2 event queue stores plain values only, so this is a promise about the
// vocabulary, checked where the promise is made.
static_assert(std::is_trivially_copyable_v<Event>);
```

键码 `code` 与按键号 `button` 都是宿主自定义的：PC 宿主映射自己平台的键符，开发板宿主映射自己的矩阵扫描。核心只比较码值，从不解码。这条分界与第六章的方向一脉相承，核心不问外界「这是什么键」，只记录「这个码按下了」。

## RingQueue：一格留空的环

事件得有地方排队。队列住 `visor::base::container`，是从 Cinux 移植的第二个件（第一个是第三章的位操作库），源文件对着 `include/cinux/container/ring_queue.hpp` 改写：命名空间、模板参数名与 `noexcept` 按 visor 口径，队列的骨架原样保留。

```cpp
template <typename Element, std::size_t kCapacity>
class RingQueue {
    static_assert(std::is_trivially_copyable_v<Element>,
                  "RingQueue stores plain values only: it is a fixed block of slots, not a heap");
    static_assert(kCapacity >= 2,
                  "One slot is sacrificed to tell full from empty, so two is the useful minimum");
```

骨架的巧妙处在「一格留空」：`kCapacity` 个槽位只装 `kCapacity-1` 个元素，永远空着一格。牺牲一格换的是满与空的区分——头尾索引相等时队列必空，头追上尾的前一格时队列必满，一个比较就够，不需要额外的计数变量。将来队列要跨线程（单生产者单消费者，SPSC），升级只需要把头尾两个索引换成原子量，布局不用动。本批的事件循环单线程，头文件注释把这条并发姿态写明了。

满的策略是拒新不挤旧：`push` 在队列满时返回 `false`，新元素丢弃，老元素原样。挤旧（覆盖最老的元素）在输入场景里是灾难，丢掉一个 KeyUp，对应按键就在核心的世界里永远按着。谁丢事件、丢了几个，由宿主自己数（下一节离屏宿主就是这么做的），队列只负责说「装不下了」。

我们移植时在测试里亲手吃过一次这条语义的亏：给离屏宿主写满载用例，按「十六槽装十六个」写，测试红了才发现十六槽装的是十五。测试逮住作者，跟第一章是同一套体验。

## 时钟：会回绕的毫秒

时钟是宿主表新项里最不起眼、也最容易埋雷的一个：

```cpp
std::uint64_t (*clock_ms)(void* context) noexcept = nullptr;
```

返回自宿主纪元（host-chosen epoch，宿主自己定的起点，PC 上通常是开机，开发板上通常是上电）起的毫秒数。`std::uint64_t` 毫秒要用五百多万年才回绕，嵌入式产品一辈子碰不到，但我们照样教回绕，因为契约必须写死：值可能回绕，时长一律用无符号减法算（`now - then`），绝不比较绝对值大小。无符号减法在回绕后仍然给出正确时长，这是 C 语言定死的算术；一旦有人写了 `if (now > then)`，回绕那一瞬间它就错。测试会把你送到回绕边界上亲眼看一遍，验收一节有这个实验。

## 宿主表三项长齐

第六章的表只有 `flush`，现在三项齐了：

```cpp
struct HostTable {
    void (*flush)(void* context, const FlushRegion& region) noexcept = nullptr;
    bool (*poll_event)(void* context, Event& out) noexcept = nullptr;
    std::uint64_t (*clock_ms)(void* context) noexcept = nullptr;
    void* context = nullptr;
};
```

方向与 `flush` 相反但道理相同：显示是核心推给宿主，事件与时钟是核心从宿主拉。队列与同步是宿主的责任，核心只管拉到了什么。

核心侧配了两个判空包装，空表项与空队列读作同一个结果：

```cpp
[[nodiscard]] bool try_poll_event(const HostTable& host, Event& out) noexcept;
[[nodiscard]] std::uint64_t current_time_ms(const HostTable& host) noexcept;
```

`try_poll_event` 对没填 `poll_event` 的表和事件排空了的队列都返回 `false`，核心分不清也不需要分清这两者，正如 `submit_frame` 对空表返回 `false` 一样。`current_time_ms` 对没填 `clock_ms` 的表返回 0：时间不走了，但没有调用方会死。

这两个包装的住处有一段真实的评审插曲。它们最初写成头文件里的 `inline` 函数，作者 CR 时拦下：非模板的实现不住头文件，可读性差，而且实现会随 `include` 传染给每一个消费者。于是声明留在 `host.hpp`，定义搬进 `src/bindings/host.cpp`。唯一仍住在头文件里的实现是 `submit_frame`，它是模板，模板没法搬。这条边界从此立住：头文件放声明与模板，实现进源文件。

还有一个惯例顺带立住：include 谁用谁直接包含。测试文件用了 `Event` 就自己包含 `events.hpp`，不借 `host.hpp` 顺手带进来的便车。`misc-include-cleaner` 检查在岗，借车的写不过静态检查。

## 离屏宿主喂事件

`OffscreenHost` 长出三件套：合成事件入口、脚本化时钟、丢弃计数。它是测试替身，替的是将来真宿主的位置：

```cpp
[[nodiscard]] bool push_event(const Event& event) noexcept;
[[nodiscard]] int dropped_event_count() const noexcept;
void advance_ms(std::uint64_t delta) noexcept;
```

`push_event` 往内部的 `RingQueue<Event, 16>` 里塞一个事件，队列满了返回 `false` 并自己数一笔丢弃。`advance_ms` 相对推进脚本时钟，`std::uint64_t` 加法自然回绕，测试正是靠它把时钟送到类型上限旁边。三个 thunk 一起接进 `table()`：

```cpp
HostTable OffscreenHost::table() noexcept {
    return HostTable{.flush = &OffscreenHost::flush_thunk,
                     .poll_event = &OffscreenHost::poll_event_thunk,
                     .clock_ms = &OffscreenHost::clock_thunk,
                     .context = this};
}
```

第六章教过跳板函数（thunk），这里只是从一座桥变成三座。事件与时钟的桥更薄，薄到不需要再转一手成员函数：

```cpp
bool OffscreenHost::poll_event_thunk(void* context, Event& out) noexcept {
    return static_cast<OffscreenHost*>(context)->feed_.pop(out);
}

std::uint64_t OffscreenHost::clock_thunk(void* context) noexcept {
    return static_cast<OffscreenHost*>(context)->now_ms_;
}
```

测试全部只走 `table()` 这一条通道：拿到表，用 `try_poll_event` 与 `current_time_ms` 说话，不直调宿主成员。核心将来看到的就只有表，测试看到的也就只有表。

## 测试的几个回合

第一回合，五个进五个出。五个事件按序喂入，经表逐个拉出，`std::holds_alternative` 验类型、`std::get` 验字段：

```cpp
    VISOR_CHECK(feeder.push_event(visor::host::KeyDown{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerMove{.pos = PointI{5, 6}}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{7, 8}}));
    VISOR_CHECK(feeder.push_event(visor::host::KeyUp{.code = 30}));
    VISOR_CHECK(feeder.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{7, 8}}));

    VISOR_CHECK(visor::host::try_poll_event(fed, polled));
    VISOR_CHECK(std::holds_alternative<visor::host::KeyDown>(polled));
    VISOR_CHECK_EQ(std::get<visor::host::KeyDown>(polled).code, 30U);
```

第二回合，排空后的队列拒绝再给，宿主照常应答。第三回合是回绕，先看测试原文：

```cpp
    // Wraparound: park the scripted clock next to the top of its range, let it
    // cross the boundary, and read the duration by unsigned subtraction, the
    // only way the ch2 contract allows.
    feeder.advance_ms(0xFFFF'FFFF'FFFF'0000ULL);
    VISOR_CHECK_EQ(visor::host::current_time_ms(fed), 0xFFFF'FFFF'FFFF'0000ULL);
    feeder.advance_ms(0x1000ULL);
    const std::uint64_t then = 0xFFFF'FFFF'FFFF'0000ULL;
    const std::uint64_t now = visor::host::current_time_ms(fed);
    VISOR_CHECK_EQ(now - then, 0x1000ULL);
```

时钟停在 `0xFFFF'FFFF'FFFF'0000`，再走 `0x1000`，值早就跨过 `std::uint64_t` 的上限翻回头了，`now - then` 仍然读出 `0x1000`。无符号减法自己会数过边界，这就是契约敢写「时长只用无符号减法」的底气。

第四回合，满载。十六槽装十五，第十六个被拒并被数出来，先前的十五个原序原值出来：

```cpp
    for (std::uint32_t i = 0; i < OffscreenHost::kFeedCapacity - 1U; ++i) {
        VISOR_CHECK(crowded.push_event(visor::host::KeyDown{.code = i}));
    }
    VISOR_CHECK(!crowded.push_event(visor::host::KeyDown{.code = 99}));
    VISOR_CHECK_EQ(crowded.dropped_event_count(), 1);
```

容量常数 `kFeedCapacity` 是公开的，测试引用它而不是写死 15。一格留空是队列的语义，不是测试该硬编码的秘密。

## TSan 第六道

验收脚本从五道变六道。新加的 TSan（ThreadSanitizer，数据竞争检测器）与 ASan/UBSan 一样是一道独立构建：

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_TSAN=ON
cmake --build build-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure
```

两个检测器运行期互斥（各自的运行库进不了同一个进程），所以 `VISOR_TSAN` 与 `VISOR_SANITIZE` 在 configure 阶段就互斥，两个都开直接报错。本批的测试还是单线程，TSan 此刻抓不到竞争，它是铺路：等第一个线程化宿主把队列升级成原子头尾，这道检查已经在岗了。

## 验收

跑整份测试：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

整套验收现在是六道，一条命令：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

再做一次红绿实验，亲手摸回绕。编辑 `tests/host_tests.cpp`，把回绕那段的方向反过来，用绝对值比较代替无符号减法：

```cpp
    VISOR_CHECK(then < now);
```

重新构建跑测试，`then` 是 `0xFFFF'FFFF'FFFF'0000`，`now` 已经回绕成小数字，`then < now` 为假，断言红。这正是我们在契约里禁止绝对值比较的原因：不回绕的世界里它是常识，回绕边界上它就是错的。把改动复原，回到绿。

下一章（计划中）进控件树与核心侧的运行循环：事件有人消费了，循环体把「读事件、推进、提交」串成一条线。

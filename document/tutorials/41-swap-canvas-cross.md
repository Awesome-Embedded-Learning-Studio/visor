# 第四十一章 换向画布与跨架构一枪

第三十九章结尾咱们记过一笔：「交换缓冲这第三种配置没动，类注释里它还是 a later batch」。这一章把它兑现。第三十九章画布的类注释里本来就说，整幅、水平条带与后面的交换缓冲是同一模板的不同配置，`SwapCanvas` 把那句里的交换缓冲从一个承诺变成一个模板。三个设想被推导与判例逐个证伪之后它才定形，咱们把那段推导完整走一遍。同一批还有另一条线：交叉编译判例把 Cinux 链接的全部层喂给 armv7 的交叉编译器，PC 上写的代码第一次在别的架构那里交了作业。第三十九章另一句留话「SPI 真彩屏的板端验证仍是下一站」，这一枪打的正是提前能验的那一半，真板没到，过的只是编译这一关。事件词表那一侧也分了家，`include/visor/bindings/events/` 下多了 key_words、modifiers、pointer_words 三片新头，伞头 `events.hpp` 照旧一个 include 全带回来，老写法的调用点照旧能编，只带一类词的那些换成了族头，咱们不在它身上多停。

## 第三种配置：两间店

咱们翻开 `include/visor/canvas/canvas.hpp`，`SwapCanvas` 紧跟在 `Canvas` 后面，模板签名同款：

```cpp
template <base::SizeI CanvasSize, typename Format = Mono1Traits>
class SwapCanvas {
```

类注释第一句就把它的位置说清：the swap buffers of the architecture sentence made one template instead of a promise。咱们看它的成员，只有两间店：

```cpp
    constexpr void swap() noexcept { std::swap(back_, front_); }

  private:
    Canvas<CanvasSize, Format> front_{};
    Canvas<CanvasSize, Format> back_{};
```

词汇咱们对着 `Canvas` 数下来：`Traits`、`Pixel`、`kWidth`、`kHeight`、`kStrideBytes`，`fill`、`set_clear`、`set`、`get`、`pixels`，全都在。差的是 `kBufferBytes`，`SwapCanvas` 没有这个词，两间店各自的字节数由 `Canvas` 自己报，后文交叉探针恰好借 `Canvas` 的这个词来问它。绘制动词与 `pixels()` 都落在后店，一轮画完调 `swap()` 两店换位，下一轮画进另一间，前台持着上一轮刚提交的完整像素。`get` 读的也是后店，越界读本格式的零，与 `Canvas` 同一条界线。所以一个跑循环、一支画笔、一张宿主表，原来接 `Canvas` 的地方换 `SwapCanvas` 照样接，同一副模型换一种配置，这就是类注释里 one model, different configurations 的意思。

`Canvas` 没有的动词有两个，`swap` 咱们前面见过，另一个是：

```cpp
    /// The front store's raw rows: what the host last received, for a host
    /// or a judgment that reads while the next round renders.
    [[nodiscard]] constexpr std::span<const std::uint8_t> front_pixels() const noexcept {
        return front_.pixels();
    }
```

这个口子是给扫描输出型宿主的，咱们把它的用法说清：扫描输出型宿主不在 flush 时拷贝，而是按自己的节拍扫描前台。因为绘制无副作用、每轮整窗重放，前台永远持有一个完整轮，读它读不出半张帧。类注释同一段把这层意思也说完，the front is always one complete round old, never a partial frame。

## 三个设想，逐个证伪

定形之前咱们不妨假想三条岔路，一条判例守住，两条推导堵死，`SwapCanvas` 才长成上面这个样子。

第一条设想：提交窗口要紧跟刚重画的窗口。给换向画布记一笔窗口，swap 时把它带走，好像前台得知道自己缺哪块。咱们把第一条设想推开看：一轮的重放按 `render_window` 与 `render_strips` 的口径画整窗，窗口是什么由循环说了算，画布不欠提交任何东西，提交是宿主的事，第四十章的计划刚把它接走。画布只管两间店换手，一个字的状态都不该添。真按第一条设想做，画布就得添一个成员、一个查询口子，换来的只是一个宿主自己已经知道的数。

咱们把第二条设想摆出来：条带形状的换向画布不算双缓冲。20 行的屏配 8 行的条带画布，两间店各只有一条带的存储，拼不出一张整屏，看起来不像教科书里的双缓冲。堵死它的是交替的单位：换向交替的是轮，不是屏。一轮里同一个后店条带被三个窗口轮流画过、轮流提交，轮末一 swap，前台持的是上一个完整轮在同一位置的内容。条带形状那段判例守的就是这件事，条带形状照样成立，只是「上一轮」的同位置是一条带，不是一张屏。

第三条设想最有迷惑性：两间店不同步，下一轮画之前把前台拷回后台，不然旧内容就丢了。初稿真写过 `copy_front_to_back`，后来删掉。咱们把推导走全。软 blit 在经典双缓冲里欠的债有它的前提：存在一块比窗口大的缓冲，更新只动它的一角，缓冲的其余部分是上一帧的旧像素，换页前得有人把旧像素搬齐。visor 的模型里没有这个前提：缓冲恰好装下正在渲染的窗口，一轮画的是整窗，窗外的像素根本不在缓冲里，没有会变陈旧的角落。后店下一轮从头整块重画，前台本来就该持有上一轮，谁也不欠谁一次拷贝。类注释在末尾把话写死：

```cpp
 * pace: the front is always one complete round old, never a partial frame,
 * because visor's buffers store exactly the window being rendered -- there
 * is no partial update of a big buffer to grow stale in the first place,
 * and therefore no software blit for a double buffer to owe.
```

## 整幅两轮：逐像素对参考

`tests/render_tests.cpp` 的换向判例沿用第三十九章的关卡场景，20x20 的画面、单色预设。咱们看整幅形状那段。两个标记都改成暗色，开场只亮尾标记，然后把整屏申报成黑底：

```cpp
GateScene scene{&visor::ui::kLightTheme};
scene.tail_dot.fill = visor::canvas::ColorRole::Background;
scene.head_dot.fill = visor::canvas::ColorRole::Background;
scene.tail_dot.set_visible(true);
using Swap = visor::canvas::SwapCanvas<kScreenSize>;
visor::host::OffscreenHost swapped(kScreen, kScreen);
Swap buffers;
buffers.set_clear(false);
visor::ui::render_strips(scene.root, buffers, kScreenSize, swapped.table());
buffers.swap();
```

咱们把可见性翻转，第二轮头标记亮尾标记灭，再走一遍 `render_strips` 加 `swap`。然后判例问两件事。第一件问提交的内容：另起整幅 `Canvas` 把当前树 `render_window` 一遍当参考，与换向宿主的屏逐像素比，400 个点全对，整窗重放落在哪一间店都是同一幅画，这是判例的兼容半边。第二件问交替本身：

```cpp
VISOR_CHECK(!buffers.get(PointI{13, 18}));
VISOR_CHECK(buffers.get(PointI{3, 3}));
```

第二次 swap 之后后店该持开场态：尾标记位 (13,18) 是暗的，头标记位 (3,3) 从没画过标记、是亮地。注释把这一探针的分量说明白，that is the alternation itself, not just a second full repaint landing on the same store。咱们假想 swap 是个空操作，第二轮也画进同一间店，后店就全是第二轮的内容，这两根探针当场翻。判例要证的正是换手真的发生了。

## 条带形状：前台永远持有上一个完整轮

第二段把画布换成条带形状，`SwapCanvas<SizeI{.w = 20, .h = 8}>`，其余全同。一轮里 `render_strips` 让后店条带依次扮演三个窗口，8 加 8 加 4 行，每个窗口画完就提交，轮末一次 swap。咱们看末窗口：尾标记住在场景 16 到 20 行，落在第三窗口，场景行 18 在条带自己的坐标里是第 2 行。走完两轮，判例只留一根探针：

```cpp
VISOR_CHECK(!buffers.get(PointI{13, 2}));
```

读的是后店条带的 (13,2)。第一轮在那里画了暗标记，第二轮标记隐藏、同一位置重画成亮地。第二次 swap 之后后店回到第一轮的内容，探针要暗。咱们要是让 swap 空转，后店就停在第二轮的亮地上，红。前台那一侧没有直接探针，注释把它的身份说清：the front always holds the previous complete round and a scanning host would read one whole round back, never a partial frame。扫描型宿主读前台，读到的永远是一个完整的旧轮，这正是第二条设想被堵死后换向画布剩下的承诺。

咱们把这两段判例的分工再摆一下。整幅那段证的是词汇兼容加交替，条带那段证的是交替在第三种缓冲形状下仍是整轮的。这两件证完，加上第三十九章已经守着的整幅与条带，同一对参数的三种配置就都有判例守着了。

## 探针：全部层喂给交叉编译器

咱们换一条线：真 SPI 彩屏与 Cinux 两条验收线的目标硬件都不是桌面 x86，核心代码得证明自己在别的架构上编得过。这一批立了两个文件。`tests/cross_probe.cpp` 是探针，文件头的注释把自己的站位说清：

```cpp
// A cross-compilation probe: the layers a Cinux build links, exercised for
// a target compiler. This file is deliberately outside the desktop test
// binary -- the cross judgment hands it to the cross toolchain, so the
// whitelist layers prove they compile somewhere other than the build host.
```

探针的主体是一张 include 清单，46 个项目头，Cinux 链接的每一层都在，咱们抽几行看它的铺法：

```cpp
#include "visor/base/containers/ring_queue.hpp"
#include "visor/bindings/cinux/event_wire.hpp"
#include "visor/bindings/cinux/frame_expand.hpp"
#include "visor/canvas/pixel_convert.hpp"
#include "visor/ui/widgets/textbox/textbox.hpp"
```

咱们把单子念一遍：base 的几何位库容器文本、canvas 的画布颜色转换 traits、draw 的画笔字模、bindings 的 display、host、evdev 与 cinux、ui 从画笔主题到十个控件，每层都在单子上。清单本身就是判据，注释明说 the layers compile for the target by being included。清单之外探针还做真实的实例化，让目标编译器看见桌面构建看见的同一批模板：

```cpp
template <visor::base::SizeI Screen, typename Format>
bool exercise_canvas_family() {
    visor::canvas::Canvas<Screen, Format> full;
    visor::canvas::SwapCanvas<Screen, Format> swap;
    full.set_clear(true);
    swap.set_clear(false);
    const bool wrote =
        swap.set(visor::base::PointI{1, 1}, Format::from_ink(visor::canvas::Ink::white()));
    swap.swap();
    const bool lit_corner = swap.get(visor::base::PointI{1, 1});
    return wrote && lit_corner && full.pixels().size() == swap.pixels().size() &&
           swap.front_pixels().size() == full.kBufferBytes;
}
```

咱们看这一段怎么实例化：它拿 16x8 的小屏把 `Canvas` 与 `SwapCanvas` 在三档格式下各实例化一遍，画一笔、换一次手、读回来，连 `front_pixels` 的字节数都与 `kBufferBytes` 对上。再往下咱们逐层数：几何的 intersect 与 bounding_box、位库的 mask_bit、RingQueue 的推与弹、UTF-8 的解码、cinux 帧展开的单色与 565 两个方向、事件翻译、主题的鸢尾蓝，每层都有一次按名的触碰。cinux 那 565 向正是第四十章末尾看过的 `expand_region_rgb565`，探针喂它一条四像素的行，再把展开后的字读回来对。函数是匿名的，桌面测试二进制不链接它，整个文件生来就是给交叉编译器读的。

## 候选表、退出码与大声失败

`tests/cross_tests.cpp` 是驱动探针的判例。咱们把工具链的找法也看一眼，判例不写死，按候选表从上往下找：

```cpp
const std::array<const char*, 3> candidates = {
    "/opt/arm-gnu-toolchain/bin/arm-none-linux-gnueabihf-g++", // the station's own
    "arm-linux-gnueabihf-g++", // the same triple on the PATH, if installed
    "aarch64-linux-gnu-g++",   // the carried 64-bit ARM fallback
};
```

咱们把三个候选念下来。头一名是 Arm GNU 工具链的 armv7 编译器，注释叫它 the station's own。i.MX6ULL 是站机上的芯片，站机就是真彩屏验收站要对接的目标板，一台跑 32 位 Linux ARM 的机器，char 在它的 ABI 里默认无符号。这里的站说的是验收梯队的一站，跟第三十九章的站位尺寸说的示例屏不是一回事。第二名是 PATH 上的同三元组，第三名是系统的 aarch64 兜底。带路径的候选拿文件存在性查，裸名字拿 `command -v` 查，找到一个就停。三个都缺席时判例不走运通过，而是大声失败，跟 X11 判例拉不起 Xvfb 就报是同一条线：

```cpp
VISOR_CHECK(false && "no cross toolchain found (arm-none-linux-gnueabihf at /opt, "
                     "arm-linux-gnueabihf-g++, aarch64-linux-gnu-g++)");
```

咱们读失败消息，三个名字全列出来，机器上装哪个、该装哪个，一眼有数。

找到了咱们就按项目自己的口径喂探针：

```cpp
const std::string command = compiler + " -std=c++23 -fno-exceptions -fno-rtti -Wall -Wextra" +
                            " -Werror -I" + include.string() + " -c " + source.string() +
                            " -o " + object.string() + " 2>&1";
```

咱们把这行命令逐段看。旗子与桌面构建同一套，C++23、关异常、关 RTTI、警告当错误，include 给到仓库的 `include/`。对象文件落在临时目录的 `visor-cross-probe.o`，退出码零、文件在、大小非零，三样都过判例就收尾删掉对象。退出码本身有个小机关，`std::system` 还回来的是 wait status 不是退出码，判例里的 `run_command` 按 POSIX 解码，`WIFEXITED` 与 `WEXITSTATUS` 两步走：

```cpp
const int status = std::system(command.c_str());
if (status == -1 || !WIFEXITED(status)) { // NOLINT(misc-include-cleaner)
    return -1;
}
return WEXITSTATUS(status); // NOLINT(misc-include-cleaner)
```

咱们看判例驱动编译器的方式，跟 X11 判例驱动 Xvfb 是同一种做法：测试不假装自己就是那个环境，它把真的编译器、真的 X server 请来，自己只看结果。`run_command` 的写法与 fontgen 的命令行判例是同一个帮手。

## 亲手打一枪

你自己在仓库根就能打这一枪，不等测试：

```bash
/opt/arm-gnu-toolchain/bin/arm-none-linux-gnueabihf-g++ -std=c++23 -fno-exceptions -fno-rtti \
  -Wall -Wextra -Werror -I include -c tests/cross_probe.cpp -o /tmp/probe.o && file /tmp/probe.o
```

咱们这次跑出来的末行是：

```console
/tmp/probe.o: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (GNU/Linux), not stripped
```

咱们拿到 `file` 的报告，32 位的 ARM ELF，46 个头连同它们的模板全部在目标架构那边编过。判例自己打的是同一枪，只是对象路径换成临时目录里的 `visor-cross-probe.o`，判完即删。工具链没装的话，命令本身报找不到文件，判例那边会落到候选表的下一格，机器上一格格兜下去，最后一格也没有才喊出来。

## 红绿实验

两根探针咱们都跑过核过。

咱们从最省事的第一根做起：把 `swap()` 的函数体改成空的，构建跑测试。两段换向判例的探针同时红，整幅那段的 (13,18) 读回亮、(3,3) 读回暗，条带那段 (13,2) 读回亮，因为两轮全画进了同一间店，后店持的是第二轮。注释里那句 not just a second full repaint landing on the same store 说的就是这一红。逐像素对参考的比对这回不红，提交出去的内容本来就是对的，红的是交替本身。改回去恢复绿。

第二根做在探针的清单上，删一个 include 再编译。咱们试两个。删掉 `visor/base/containers/ring_queue.hpp` 那行，交叉编译器当场报 `'visor::base::container' has not been declared`，别的头没有把它带进来，探针又按名用了它，编不过。删掉 `visor/canvas/pixel_convert.hpp` 那行，编得过，探针没有按名用它的符号，cinux 帧展开的 565 向在自己的实现文件里过那个转换器，清单里这一行守的是「这一层也被目标编译器读过」的本分。两相对照，清单的用法就清楚了：它数的是每一层有没有被目标编译器读过，符号级的使用各自住在自己的判例里。

## 验收

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件是汇总行以 `100% tests passed` 开头、退出码 0。两段换向判例编在 `visor.tests` 里，交叉判例也在其中，工具链或 Xvfb 缺席时它们大声报，不静默通过。咱们这轮跟第四十章是同一次跑：`100% tests passed out of 4`，四个目标全过，秒数记在那一章。

整串验收咱们一条命令跑完：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
acceptance: all green
```

六道的清单照旧：普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍，fontgen 独立构建与测试收尾。你看末行的 `acceptance: all green`。再补一枪亲手版的交叉编译，命令与成功条件在上面一节。

## 下一站的口子

咱们把没做的照实记下。换向画布的真实消费者还没有落地，`front_pixels` 目前只有交叉探针在读，扫描输出型宿主要等 SPI 彩屏的板端验证。示例也还没换装，桌面窗口走影子帧拷贝模型，暂时用不上换向。桌面示例仍停在 Mono1 与 RGB565 两档，第三十九章挂的口子继续挂着。下一站按待办主线走第三站真 SPI 彩屏的纯逻辑半边，驱动宿主的命令流翻译加探针判据，真板探针等硬件到位。armv7 这一枪已经把 PC 半边的路铺到了硬件跟前。

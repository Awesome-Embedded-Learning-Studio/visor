# 第四十二章 Wayland 窗口宿主与可重复键

第四十一章收尾时咱们把下一站记成真 SPI 彩屏的纯逻辑半边，驱动宿主的命令流翻译加探针判据。桌面线半路送来了新货：X11 之后第二位真宿主落地，Wayland 窗口宿主接上同一张宿主表，顺带交出 Wayland 侧的第一个宿主特性，按住不放的键按节拍重复。SPI 的纯逻辑半边原样挂在待办上，本章照实接新货，下一站还是它。

`include/visor/bindings/wayland/` 下住了三片文件，咱们把名字数一遍，宿主本体 `wayland_window_host.hpp`、输入翻译 `event_wire.hpp`、节拍器 `key_repeat.hpp`。定位咱们念宿主的类注释，the second real host: a Wayland window carrying the table's three entries, the X11 window host's own stance on a new protocol。

## 第二位真宿主，同一张表

咱们从开门的动词读起：

```cpp
[[nodiscard]] static std::optional<WaylandWindowHost>
open(const char* display_name, int width, int height, int scale = 1,
     PixelFormat format = PixelFormat::Mono1) noexcept;
```

参数咱们对着 X11 宿主念，同款。`display_name` 给 nullptr 就读环境里的 `WAYLAND_DISPLAY`，与 `wl_display_connect` 自己的默认一致。`scale` 把每个画布像素在末步放大成 scale 乘 scale 的方块，OLED 尺寸的画布在桌面显示器上才看得清，因子必须是整数，X11 宿主画的就是同一条线。

失败的语义咱们念类注释的原话，a missing display is an answer, not an emergency，返回空的 optional，绝不崩溃，scale 小于 1 同样给空 optional。

表咱们数下来还是三个口子，flush、poll_event、clock_ms，加上 `HostInfo` 里的屏尺寸与格式，核心拿到的还是熟悉的表。窗口尺寸在出生时就定在画布尺寸上，合成器事后建议的尺寸一律应答后不理，咱们把类注释的理由念出来，a new size would only add area nobody paints，多出来的面积没有人画。

宿主的计数器与观察口咱们挨个数。`flush_count` 与 `rejected_count` 分别数成功的提交与拒收，`submitted_words` 把最后一次填进的窗口缓冲词交出来给判例读，`display()` 与 `surface()` 把协议句柄借给要挂自己回调的判例。事件队列的容量是 16，满了丢新来的并计数，跟离屏宿主的事件队列同一条界线。

搬家有一处咱们要留神。表里带着宿主自己的地址，搬走之后旧表就成了悬空的地址，头文件注释明说 take the table again after moving the host, or do not move it while a table is in use，要么搬完再取表，要么用表期间不搬。

## 连接与注册表

`open()` 的流程咱们顺着走一遍。连上显示器之后拿到注册表，它是合成器报出的全局对象清单，咱们挂上监听器做一次 roundtrip，roundtrip 是把请求送出去等回音收齐的往返，四样全局各自认领，合成器、共享内存、xdg_wm_base、seat。绑定的版本各有上限：

```cpp
constexpr std::uint32_t kCompositorVersion = 1;
constexpr std::uint32_t kShmVersion = 1;
constexpr std::uint32_t kWmBaseVersion = 1;
constexpr std::uint32_t kSeatVersion = 5;
```

版本咱们念下来，前三样绑 1 就够说话，seat 想要 5，因为指针的 frame 词与滚轮的 axis_discrete 词从 5 才有。绑定版本被合成器广告的版本压住，老合成器照样能跑，只是滚轮词永远不来。wm_base 的 ping 词来了就回 pong，合成器问客户端活没活着，回答本身就是全部动作。

共享内存咱们看它住在哪，一块 memfd 当池子，装下两块窗口缓冲，匿名，在文件系统里没有名字。协议头文件只在实现文件里出现，头文件这边对协议的 C 类型只做不透明指针的前向声明，核心与消费者看不见一个 wayland 字。

窗口的诞生咱们跟着 xdg_shell 的规定动作走。建表面，拿 xdg_surface，拿 toplevel，设标题，然后一次空 commit 去要第一个 configure。等待有预算，1000 毫秒为限、10 毫秒一片地等，configure 到了就应答。

应答之后咱们看它交什么，立刻提交一次整幅暗画布，让合成器从第一次 commit 起手里就有真东西，后面的 flush 画在已知状态上。open() 的开场提交不计入 flush_count，判例专门有断言守住它。

## 两块窗口缓冲，像素怎么上屏

Wayland 不收 1bpp 缓冲，上屏格式是 XRGB8888，Mono1 与 RGB565 都在出口处展开，与每一章颜色线的展开同一个词。flush 的路线咱们分开看，是两跳。第一跳把区域画进影子帧，影子按画布格式打包，是屏幕内容的唯一真相。第二跳把影子的指定区域展开成 32 位词写进空闲的窗口缓冲，然后一次 attach-damage-commit 把缓冲交给合成器，damage 上报的坐标也乘过 scale，说的是窗口坐标。

两块缓冲轮换，这是 Wayland 客户端的本分。咱们念它欠合成器什么，客户端不许重画一块还没被还回来的缓冲。两块都在合成器手里时，flush 在自己的等待循环里 dispatch 到一块被释放为止，预算 1000 毫秒。注释把立场的名字说清，对单线程的调用者来说阻塞是诚实的选择，一个永不还缓冲的合成器就是垂死的显示器，循环从错误一侧出去。

轮换带来一个问题，咱们把它摆出来，挑中的缓冲可能持着旧画面。解法是纪元：

```cpp
    if (chosen->shadow_epoch != shadow_epoch_) {
        area = base::RectI{
            .pos = base::PointI{0, 0},
            .size = base::SizeI{.w = width_, .h = height_},
        };
    }
```

纪元的机制咱们对着代码念，影子每被画一次纪元加一，每块缓冲记自己匹配的纪元。落后的缓冲必须整幅重画，追平的缓冲只画区域，注释说得很清楚，the shadow is the only truth, and a stale buffer mixes history with the caller's area。

出词的动词咱们看 `canvas_pixel_word`，每个画布像素落成 scale 乘 scale 的方块，按三档格式出词。Mono1 位是 1 给 0xffffffff、是 0 给 0xff000000，RGB565 走 `rgb565_to_xrgb8888`，XRGB8888 原词直通。暗词里 X 字节也填 0xff，注释解释了为什么，the X byte is padding no compositor reads, so both dark words display the same black，配对让字节镜像保持诚实，读回缓冲的判据才对得上。

回读咱们要不到，Wayland 没有从合成器那边的回读，能读的只有宿主自己写下的词，`submitted_words` 就是判据的读法。影子帧里 Mono1 的画法是逐像素 MSB 前进，按字节的快路仍住在离屏宿主里，注释留了话，a second shopper is when it moves out，第二个消费者出现时再收进 base。

搬家在缓冲这里有专门的修理，咱们看它修什么。协议监听器记着代理出生时的宿主地址，搬走之后合成器还会把释放词送进旧地址。`rewire()` 用 `wl_proxy_set_user_data` 把每个代理的用户数据改成新地址，注释把跳过它的后果写得很重，a compositor happily releasing buffers into the corpse while the living host waits forever for its own buffers back。

## 事件线：指针、滚轮与裸 evdev 键

输入词的翻译咱们到 `event_wire` 里看，全部是纯函数。指针这边，enter 词自带位置又送出 set_cursor 要的 serial，一身两用兼作移动词，顺带把合成器自己的光标请走，桌面画自己的指针。位置词说的是窗口像素，核心要画布像素，除回 scale 再造词，负坐标照传，界外裁剪是核心的事。leave 词不带位置，所以没有词可造。

滚轮咱们看它怎么攒词，只认 detent，axis_discrete 词攒进步数，frame 词收组时一次性出词：

```cpp
    static void pointer_frame(void* data, [[maybe_unused]] wl_pointer* pointer) noexcept {
        auto* host = static_cast<WaylandWindowHost*>(data);
        offer(*host, translate_wheel(host->pending_wheel_dx_, host->pending_wheel_dy_,
                                     host->pointer_pos_, host->modifiers_));
        host->pending_wheel_dx_ = 0;
        host->pending_wheel_dy_ = 0;
    }
```

连续滚动的长度词不读，核心数的是格。按钮的编号咱们对着判例念，原始 evdev 常量，0x110 对 1、0x111 对 2、0x112 对 3，同一颗物理键在两位宿主那里号码相同。侧键及以后翻译成无词，与空表项同一种诚实的拒绝。

键盘这边咱们走裸 evdev 路线。协议交来的就是内核自己的键码，没有 X11 那边的加 8，第三十四章 evdev 翻译的整条线直接适用。Backspace、Tab、Enter、Space 本来就同号直过，其余键码原号过，修饰键本身也是词，核心比较键码、从不解码。

方向键咱们单独搬出来看。evdev 的 103 到 108 落在可打印 ASCII 的范围里，与字母数字正面相碰，翻译层把四支方向键搬去核心的带外值，与 evdev 翻译器当年做的是同一件事。

keymap 的文件描述符咱们看它的下场，合成器递来就关掉不读，注释说 descriptors are the compositor's gift, not the host's to keep，宿主读裸键码，键映射用不上。修饰掩码只看 depressed 那一面，latched 与 locked 是核心四面旗装不下的粘滞状态，掩码里锁与数字的位不翻译。翻译层不产文本，字母数字保持键码，文本控件看见的空格键是 57，与 evdev 翻译器同一种拒绝。

## 可重复键：客户端自己的节拍器

按住一个键不放，桌面上的体验咱们都熟，延迟一阵之后按固定频率重复。Wayland 协议把节奏参数用 repeat_info 词发给客户端，重复词由客户端自己造，X11 服务器替客户端生成的重复词，在这边是宿主的本分。visor 的宿主不读 keymap，系统里现成的重复帮手都用不上，所以有了一台自己的节拍器。可测性咱们看类的头注释，all deadlines use the caller's monotonic microseconds, so tests need neither a seat nor sleeps：

```cpp
    [[nodiscard]] std::optional<std::uint32_t> poll(std::uint64_t now_us) noexcept {
        if (!key_.has_value() || now_us < next_us_) {
            return std::nullopt;
        }
        next_us_ += (((now_us - next_us_) / interval_us_) + 1) * interval_us_;
        return key_;
    }
```

咱们看推进的写法。到点之后下一次的时间不跟着当前时刻走，而是沿着节拍表走，睡醒收敛与保节奏都从它来。应用卡了一阵醒来，错过的间隔并成一次按下，不把一串旧输入灌进队列，下一次的到点仍落在原节拍上。

归属咱们到 `key()` 里看。最后一次可重复的按下拥有计时器，后来者顶掉前者，松开前者不恢复更早的键，头注释明说 releasing it does not resume an older key。

修饰键的按下为什么不打扰进行中的重复，咱们看 `key()` 的条件就明白，修饰键属于不可重复的按下，`key()` 不让它碰计时器。

哪些键可重复，咱们到实现里的 `key_repeats` 数，左右 Shift、Ctrl、Alt、Meta，加三个锁定键与 Compose，一律不重复，其余都重复。注释给的理由就是宿主自己的立场，modifier and lock keys change state once, editing keys repeat。

取消的路径咱们数下来有五条，松开自己的键、失去键盘焦点、seat 收走键盘能力、显示派发出错、速率被配成零。取消路径里的失焦顺带把修饰状态也清了。老的键盘没有 repeat_info 词，就用常规默认值顶着，间隔 40 毫秒、延迟 600 毫秒，configure 一到就换合成器自己的节奏。

节拍器怎么进宿主，咱们看 `poll_one` 的顺序，派发 socket、弹出队列、再问节拍器。节拍器产出的重复词走同一个 `translate_key`，按下的语义出，关闭请求到了之后就不再重复。

## 判例：纯逻辑的半边

`tests/wayland_tests.cpp` 守翻译层与节拍器，不需要任何显示环境。节拍器的边界判据咱们直接看断言：

```cpp
KeyRepeat repeat;
repeat.configure(25, 400, 0);
repeat.key(14, true, true, 100'000);
VISOR_CHECK(!repeat.poll(499'999).has_value());
VISOR_CHECK_EQ(repeat.poll(500'000), 14U);
VISOR_CHECK(!repeat.poll(500'000).has_value());
VISOR_CHECK(!repeat.poll(539'999).has_value());
VISOR_CHECK_EQ(repeat.poll(540'000), 14U);
```

断言咱们念成白话，每秒 25 次、延迟 400 毫秒，499999 微秒不出词、500000 出、同一微秒再问不出、539999 不出、540000 再出，延迟与节奏的边界都精确到微秒。

归属的判据咱们接着验。键 30 按下又被键 48 顶掉，松开 30 不影响 48 的时间点，修饰键 42 的按下也不影响，到 70'000 微秒出的是 48。睡醒的判据咱们配零延迟看，1'210'000 出一次之后同刻再问不出，1'240'000 的原节拍再出。失焦与拔键盘都取消，修饰键单独按永不启动。零速率把进行中的重复与将来的按下一起禁掉，重新启用需要一次新的按下，按住的旧键不会复活。repeat_info 中途更新时按新设置重起延迟，负的速率与负的延迟安全地禁用。

翻译层的判据咱们把面铺全。修饰掩码四位各亮各的旗，锁、数字、mod3、mod5 四个位一起上来什么也不亮。无名键原号过，按与抬都验，修饰随词携带。方向键四支各对核心的带外值。三个按钮在判例里写成十六进制的原始常量去对 1、2、3，注释说了防的是什么，asserted against the raw evdev words so a flipped constant cannot pass by testing itself，常量翻个身就不能拿自己证自己。移动词的负坐标照传，滚轮的符号照传，空对无词。

## 判例：测试自己拉起一台合成器

咱们看帮手类 `HeadlessWeston` 怎么守边界。它探针 socket，已应答就借用现成的，析构只收拾自己 fork 出来的进程，别人起的合成器是别人的。环境里没有 `XDG_RUNTIME_DIR` 就直接记 miss，weston 没地方放 socket。拉起的命令是：

```cpp
::execlp("weston", "weston", "--backend=headless-backend.so", "--socket", socket,
         "--idle-time=0", nullptr);
```

拉起的环境咱们念一遍，私有 socket 叫 visor-headless-test，headless 后端不画任何屏幕，weston 自己的话都送进 /dev/null，判例的输出保持干净。最多等三秒，起不来就大声失败，与 X11 判例拉不起 Xvfb 就报是同一种做法。

像素判据咱们读 `submitted_words`，帮手 `compare_window` 逐词走整个放大后的窗口，每个词要么是亮词要么是暗词，注释给了它的身份，the client-side twin of the X11 readback judgment。X11 那边词从服务器的 XGetImage 来，这边词从宿主自己填的共享内存帧来，两边都在证同一件事。

十段判据咱们挨个念。表的三口都接上、时钟在走、安静的会话 poll 出安静。整幅逐位对上，flush 计一。奇数 x 的部分区域只动自己，区域读回新、余部保持旧词、字节边缘的邻位完好。

首字节偏移为 16 的判据咱们单独看，它守的是 pixels 的指向，指的是区域自己的首字节，多跳一字节就会把邻位的位读进来，注释连病灶都写了，a second hop past it would read two bytes deep。scale 3 的方块逐词验。搬家之后部分区域不复活旧像素，纪元跟着对象走、监听器改挂新地址。RGB565 的两根探针是纯红与纯蓝，0xF800 与 0x001F，区域外的词保持暗底。XRGB8888 原词直通，X 字节也在。拒收线上四种错各计一拒、flush 保持零，scale 0 与不存在的 socket 都给空 optional。

末段咱们看帧回调，判例自己的话最准，a frame callback is the compositor's throttle hint for the next repaint, not proof that this buffer reached the glass。等它落地证明的是请求、提交、应答一圈走通，最多等两秒，像素本身由前面的读回证。

## 构建面：系统库直连

构建面咱们对着 CMake 念，口径与 X11 同一条线，系统库直连，不 vendor 任何东西。libwayland 用 pkg-config 找：

```cmake
pkg_check_modules(VISOR_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client>=1.26)
```

1.26 的下限咱们查过，原因很具体，指针监听表点了 warp 槽位，只有 1.26 时代的头文件认识，老头文件在编译期就翻脸，下限写在明处比运行期猜要好。

xdg-shell 的客户端代码由系统的 wayland-scanner 从系统的协议 XML 现场生成，client-header 加 private-code 两份产物编进库。生成的代码咱们分得清归属，它不属于 visor 自家代码，SYSTEM include 让 clang-tidy 与 clang-format 不把它当 visor 代码读。`_GNU_SOURCE` 为 memfd_create 而开。

链接咱们看两行，libwayland 走 PRIVATE，系统库不漏进下游的链接行，PUBLIC 那边只给 visor.core。测试目标把 `visor.host.wayland` 加进清单，示例 `wayland_window` 同样只链它与 `visor.host`。

## 亲手开一扇窗

示例咱们从文件头念起，`examples/wayland_window.cpp` 是目测线，文件头说自己是 the same eyeball lane the X11 example draws, on the second real host，X11 的示例画什么它就画什么。128 乘 64 的单色画布，scale 2 开窗，画面是亮边框套八像素的棋盘格。事件循环每 100 毫秒收一个词，按下、键码、滚轮各打一行，合成器的 close 词一到就退。按需重画是桌面层的活，文件注释明说 repainting on demand is the desktop's layer, not this lane。

你的桌面本来就是 Wayland 会话时，环境里已有 `WAYLAND_DISPLAY`，你直接跑：

```bash
./build/examples/wayland_window
```

咱们在 WSLg 下实跑，窗口开出来，stderr 报 `window up: 1 flushes, 0 rejections`，关窗即退。没有桌面的机器上，headless weston 起一个私有 socket 照样跑：

```bash
weston --backend=headless-backend.so --socket visor-demo --idle-time=0 &
WAYLAND_DISPLAY=visor-demo ./build/examples/wayland_window
```

咱们同样实跑，输出的还是 `window up: 1 flushes, 0 rejections`。用 timeout 三秒收走循环，退出码 124 是 timeout 的收条，不是示例的错。

## 红绿实验

两根咱们都亲手跑过核过，各只动一处，跑完复原。

头一根咱们动节拍器的锚。把 `poll` 里的推进式改成 `next_us_ = now_us + interval_us_`，到点时间从节拍表改成跟着醒来的时刻走。咱们跑测试恰一处红，`wayland_tests.cpp` 的 86 行，睡醒段在 1'240'000 微秒等不到词，因为新锚把下一次推到了 1'250'000。红的是保节奏的承诺，错过的间隔并成一次之外，节拍还得停在原表上。改回恢复绿。

第二根咱们动纪元。把 `put_region` 里让落后缓冲整幅重画的分支删掉，追平与落后一视同仁只画区域。咱们跑测试，`compare_window` 的逐词比对翻了两万三千零六十次，全落在 `wayland_host_tests.cpp` 的 123 行，另有 RGB565 判据的一次落在 317 行，合计 23061。咱们把挑中的缓冲查清楚。奇数 x 与搬家的三段里，挑中的是开场画过整幅暗底的一块，纪元停在开场，只画区域就把整幅提交留下的亮像素留在了暗词上。RGB565 判据里挑中的是从未画过的另一块，整幅还是 mmap 出来的零词，区域外的第三个词当场对不上。次数咱们也数得出，7400 加 7532 加 8128，再加一次，正是 23061。影子是唯一真相，靠的是整段红。改回恢复绿。

## 验收，与下一站的口子

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件你看汇总行，以 `100% tests passed` 开头、退出码 0，wayland 两份判例都编在 `visor.tests` 里，weston 或 Xvfb 缺席时它们大声报，不静默通过。

整串验收咱们走脚本。脚本如今分两道，默认的 quick 道只构建与静态检查，判例只在 `--full` 道跑。分道是后面画笔批定下的事，脚本头注释记着日子，本章的数字咱们按补跑时的 `--full` 口径记：

```console
$ VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh --full
acceptance: all green (full lane)
```

咱们本轮实跑记实数。rel 道 visor.tests 走了 135.56 秒，三个自测各 0.06、0.05、0.00 秒，合计 135.68 秒。ASan/UBSan 道 134.37 秒加自测，TSan 道 135.39 秒加自测，三道都是 4 个目标全过。格式与静态检查各扫一遍，fontgen 独立构建 2 个目标 0.06 秒。你看末行的 `acceptance: all green (full lane)`。

咱们把没做的照实记下。SPI 彩屏的纯逻辑半边仍是下一站，wayland 是半路收下的新货。示例不按需重画，桌面层还没铺过来。宿主不读 keymap，字母停留在键码，文本词不出。滚轮词要 seat 5，老合成器只有别的词。flush 等缓冲是阻塞式，帧回调驱动的重画调度还没接，桌面线将来要做帧率时那是第一站。

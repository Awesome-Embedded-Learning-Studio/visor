# 第四十四章 宿主表第四口：spawn

最近几批核心那一侧热闹，事件词表分了家，画笔长出了逐笔彩色的裸墨通道。宿主表的口子安静得多：自第八章凑齐三口，显示提交、事件收取、毫秒时钟，没人动过。本批它长出第四口：spawn，核心第一次能请操作系统生一个子程序，连同参数、环境与几件文件描述符的礼物一起交出去。仓库里三笔提交摆在一起，一口新槽带 POSIX 填充与全套判据，两笔修正是真实场景当面逼出来的，环境预算从 64 词提到 256 词，原号递入的礼物摘 CLOEXEC 封条，CLOEXEC 是 exec 成功即关的标志，封条为什么挡路咱们到封条一节细说。咱们把三笔都走一遍，孩子怎么把话递回来也在内。

## 第四口长在哪

咱们翻开 `include/visor/bindings/host.hpp`，表里现在是四个回调槽：

```cpp
struct HostTable {
    void (*flush)(void* context, const FlushRegion& region) noexcept = nullptr;
    bool (*poll_event)(void* context, Event& out) noexcept = nullptr;
    std::uint64_t (*clock_ms)(void* context) noexcept = nullptr;
    int (*spawn)(void* context, const SpawnRequest& request) noexcept = nullptr;
```

类注释给表的历史记了一笔，Ch2 carries three callbacks 说的是老三口，规格里的 Ch2 对应教程的第八章，下一句接着写 Stage 4 joined a fourth: the spawn slot, birthing programs with the host's own wiring。生程序用的是宿主自己的接线，核心只发请求。注释同时把新口的边界写明：spawn returns the child pid, or -1 when the host cannot birth，而槽位只生不收，the slot only births, it never reaps，等子程序退出归填表人。咱们把承诺念出来就是，表的第四口只管生孩子，孩子活到几时归请生的人管。

包装有两个，与 submit_region、poll、now_ms 住在同一层，咱们连着看：

```cpp
    /// Births one program through the host's wiring; -1 when the host wired
    /// no spawn (the fake stance) or the birth failed.
    [[nodiscard]] int spawn_child(const SpawnRequest& request) const noexcept;

    /// Whether the host can birth programs at all.
    [[nodiscard]] bool can_spawn() const noexcept;
```

实现咱们在 `src/bindings/host.cpp` 里看，一行见底，槽位非空就调，空则回 -1，与 poll 的 null 检查同一条纪律。

未接线的表读作不支持，注释里叫 the fake stance。住在这里的是不填回调的空表与离屏宿主，判例不因一张表缺了 spawn 就把它判坏。CMake 那边 `visor.host.posix` 单独立库，注释把理由说明白，the birth words are OS words，离屏与空表保持无 OS，槽位空着，真要养程序的适配器自己链进来。核心禁平台头的边界照旧没破：请求的词汇咱们在 `include/visor/bindings/spawn.hpp` 里看得见，全是纯 C++ 的指针与 span，fork 负责复制出一个子进程，execve 负责在子进程里换上新程序，两个平台词一个字没进核心。

## 一份请求的四样词汇

SpawnRequest 携带 path、argv、env、gifts 四样词汇与 close_other_fds 一个姿态，咱们对着声明念：

```cpp
struct SpawnRequest {
    /// The program to run, exec semantics: an absolute or resolved path,
    /// never a shell command line.
    const char* path = nullptr;

    /// The argument words, argv[0] included. The terminating null is the
    /// host's to append, so a span suffices.
    std::span<const char* const> argv;

    /// The environment words, "KEY=VALUE" each, verbatim: the host does not
    /// interpret them, the caller's knowledge decides what rides along.
    std::span<const char* const> env;

    /// The descriptors riding along, each landing at its child number.
    std::span<const FdGift> gifts;

    /// True closes every descriptor above stdio except the gift targets
    /// before the program starts -- the whitelist stance a host that owns
    /// many open channels owes the programs it raises.
    bool close_other_fds = false;
};
```

咱们把四样逐个念下来。path 只认 exec 语义，绝对路径或解析过的路径，shell 命令行进不来，想要 shell 就把 shell 本身当程序去生。argv 含 argv[0]，结尾的 null 由宿主自己补，调用方给 span 就够。env 是 KEY=VALUE 的原文串，宿主不解释，带什么由调用方的知识决定，空表另有含义，注释说 an empty env inherits the caller's environment whole，最普通的跑一个程序不必手工抄环境。

close_other_fds 是白名单姿态。为真时程序起跑前把 stdio 之外一切未赠的描述符关掉，注释把它写成宿主欠的卫生，a host that owns many open channels owes the programs it raises，咱们读下来就是，开着一堆通道的宿主把私藏原样递给孩子是不体面的。

注释里还有一句归位的话咱们记下，The slot only births, it never reaps。等孩子退出是调用方的事，礼物在调用方那一侧的半边也归调用方关，槽位两样都不揽。

## 礼物的两种形状

描述符的递送有自己的词汇，咱们看 FdGift 的两个成员：

```cpp
struct FdGift {
    int child_fd = -1;
    int parent_fd = -1;
};
```

两号相等是原号递入，描述符保持自己的号码过出生，注释的比方是 the way a bridge hands its sockets to the program it raises。两号不等是重定向，dup2 把 parent_fd 的孪生落到 child_fd 号上，注释叫 the private-channel trick that turns a socketpair end into stdio，socketpair 的一端变成孩子的标准输入输出，老手法。判据里两种形状咱们都会见到现场。

## POSIX 填充把生日走完

POSIX 填充咱们从声明读起，`include/visor/bindings/posix/spawn.hpp` 只声明一个函数，签名与槽位一致，表填人可以直接指名。注释说 context 不用，a birth needs no host state beyond the request itself，一次出生除了请求本身不需要宿主状态。

实现咱们对着 `src/bindings/posix/spawn.cpp` 逐段看。入口查四样：path 空、argv 空、argv 超 64 词、env 超预算，任何一样命中回 -1，出生没发生就干净地没发生。预算是两个常量：

```cpp
constexpr std::size_t kMaxArgWords = 64;
constexpr std::size_t kMaxEnvWords = 256;
```

词表住在栈上，注释说 the exec words ride the stack，需要更多词的程序算不上桌面公民。env 预算为什么大四倍，注释给了理由，an inherited desktop environment runs to dozens of words on its own，何况继承加覆写的姿态递的是全表。咱们在本机数过，`env | wc -l` 报 76，光继承就要 76 个词，64 的预算装不下任何一张真实的桌面环境。

随后 fork，父进程拿到孩子的 pid 直接返回，孩子那一半咱们跟着按次序看三件事。

礼物落位是第一件。原号递入的礼物走 fcntl 取旗字，发现 FD_CLOEXEC 就摘掉，理由咱们留到封条一节细说。重定向的礼物走 dup2，注释说新号天生旗净，the redirect gift's new number arrives flag-clean by dup2's own contract。任何一步失败，孩子 _exit(127) 退出，注释把 127 叫 the classic "the exec never happened"，孩子不回调用方，调用方只看得见退出码。

关未赠的描述符是第二件，close_other_fds 为真时调 close_ungifted，咱们看它从 3 号扫到 sysconf(_SC_OPEN_MAX)，凡是没被礼物认领的号码一律关掉。注释说明关一个已经关着的 fd 不算失败，the program was promised nothing about those numbers。

exec words 装车是第三件，argv 与 env 各自拷进栈上的数组，结尾补 null，咱们看 env 空表的专门走法：

```cpp
        ::execve(request.path, const_cast<char* const*>(argv.data()),
                 request.env.empty() ? const_cast<char* const*>(environ)
                                     : const_cast<char* const*>(envp.data()));
        _exit(127);
```

execve 要 char* 数组，请求说的是 const 词，咱们读文件头注释，它把一对 const_cast 叫 the interface's dress code，内核只读它们，转换只是接口的礼仪。execve 本身失败也走 _exit(127)，所以调用方见到的失败只有两种形态，-1 是没生成，127 是生成了但没走到 exec，或 exec 本身失败。

## 判据怎么把话收回来

`tests/spawn_tests.cpp` 的判据咱们看下来全靠同一个模式，把孩子的话从赠出的通道里读回来。家当两样，头一样是 Answer，把一次出生的全部回音收在一起，孩子写下的话加留下的退出码：

```cpp
struct Answer {
    std::string words;
    int exit_status = -1;
};
```

collect 是另一样，拿着读端与 pid 收尾，读通道到 EOF 再 waitpid。注释说明读会阻塞，答案要么完整到、要么判据挂住，either way the truth gets said，咱们看，两种下场都比说谎强。

开场两查咱们跟着过：空表摆出来，can_spawn() 假、spawn_child(SpawnRequest{}) 回 -1，未接线的立场有了判据守着。随后判例看一眼 /bin/sh 在不在，缺席就 println 一行 SKIP 响亮返回。孩子全请 /bin/sh 当，类 Unix 机器上它最不挑，`-c` 一句脚本就能把话写进赠出的 fd。

## 五段现场

五段现场咱们挨段看。第一段重定向形，pipe 造通道，礼物把写端落到孩子的 1 号：

```cpp
const FdGift gift{.child_fd = 1, .parent_fd = channel[1]};
const std::array argv{"/bin/sh", "-c", "printf ok"};
```

孩子是 sh，printf 把 ok 写进 stdout，stdout 已经是通道写端的孪生。父进程关掉自己那半边，注释说 the caller's half is the caller's to close，collect 收话，咱们收回来的正是退出码 0 加一句 ok。

第二段封条幸存，是摘封条的修正带来的现场，咱们到封条一节细看。

第三段咱们看一件请求怎么证三件事，原号递入、换环境、白名单。礼物原号，child_fd 与 parent_fd 都是 channel[1]，close_other_fds 真，env 只带一个词：

```cpp
const std::array env_words{"VISOR_SPAWN_MARK=carry"};
```

父进程从来没导出过 VISOR_SPAWN_MARK，孩子还能收到 carry，咱们看它只能从表里递进来，环境替换就此证了。孩子脚本把 $VISOR_SPAWN_MARK 写回通道，收到的正是 carry。close_other_fds 是真的，话还能从赠出的号码里回来，白名单保住礼物也顺带证了，未赠号码的命运由第五段接着查。

第四段肥环境，提预算的修正拿它守门。一百个 SPAWN_FILL 填充词陪跑一个带标记词，咱们看孩子收回 fat 即证全表送达：

```cpp
std::array<std::array<char, 24>, 100> fillers{};
std::array<const char*, 101> env_words{};
```

第五段白名单双向，同一个 9 号两种命运，咱们看父进程用 dup2 把一个描述符停在 9 号，孩子脚本看 /proc/self/fd/9 在不在：

```cpp
const char* const probe_script =
    "if test -e /proc/self/fd/9; then printf leak; else printf clean; fi";
```

close_other_fds 真，9 号没被礼物认领，孩子报告 clean。开关假，描述符随 fork 活下来，孩子报告 leak。注释把 /proc/self/fd 叫 the Linux eye，咱们借的正是 Linux 的眼睛，看孩子手里的 fd 表。

## 一次真实的出生被当面拒掉

预算的初版两边都是 64。argv 的 64 至今没被谁打穿，env 的 64 当晚就被打穿了。真实场景摆在那里：桌面的继承环境自身几十个词，咱们在本机数过 76 个，注释里设想的桥，是要把 socket 递给自己生养的程序的宿主适配层，姿态是全表继承加覆写，它要把整张环境表拼成自己的 env 词交进来，继承的几十词加覆写，64 的预算当面拒掉一次真实的出生，孩子压根没生出来。修正把 kMaxEnvWords 提到 256，栈上二千字出头仍然轻，理由也写进了常量旁边的注释。守门的判例就是第四段现场，预算回缩到 64 时它当场翻红，红绿实验里咱们亲手做过一遍。

## 封条不摘，通道当面关掉

第二笔修正对付一个只在原号递入时出现的陷阱。CLOEXEC 的本义咱们开头记过，exec 成功即关，父进程为了自己的卫生会给描述符打上封条，注释说 the bridge's sockets all carry the mark，桥的 socket 全带标记，免得哪条通道被意外生的程序继承走。麻烦在于原号递入的礼物带着调用方的全部标志过 fork，封条若还在，execve 落地的一瞬会把刚赠出的通道当面关掉，咱们看礼物就成了空头支票。

修正在孩子那一侧，咱们看孩子怎么处理每件原号礼物，逐件取一次旗字，发现 FD_CLOEXEC 就摘掉再交给 exec：

```cpp
const int flags = ::fcntl(gift.child_fd, F_GETFD);
if (flags < 0) {
    _exit(127);
}
if ((flags & FD_CLOEXEC) != 0 &&
    ::fcntl(gift.child_fd, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
    _exit(127);
}
```

dup2 落位的礼物咱们不用管，dup2 造的新号天生不带标志，前面引过的 flag-clean by dup2's own contract 说的就是它。

守门的判例是第二段现场。咱们看 pipe2 带 O_CLOEXEC 造通道，双端都带封条，礼物原号递入，脚本把 marked 写回被标记的号码：

```cpp
VISOR_CHECK(::pipe2(channel.data(), O_CLOEXEC) == 0);
```

封条摘掉了，孩子 exec 之后仍从被标记的 fd 号里把 marked 递回来。封条不摘的话判例当场翻红，红绿实验里咱们同样亲手做过一遍。

## 红绿实验

两根探针咱们都亲手跑过核过，各改一处，构建跑测试，看红在哪一行，再复原。

预算探针动手改：把 kMaxEnvWords 从 256 改回 64。肥环境判据翻红两处，spawn_tests.cpp 的 177 行 child > 0 与 181 行 words == "fat"。预算查在入口、fork 之前，101 个词超过 64，spawn_via_posix 回 -1，孩子压根没生，pid 断言当场倒，话也就收不回来。咱们复原后再跑，绿。

封条探针改另一处：把摘封条的分支整个拿掉，回到摘封条修正落地之前的版本。marked 判据翻红两处，spawn_tests.cpp 的 118 行 exit_status == 0 与 119 行 words == "marked"。execve 落地的一瞬把刚赠出的通道关了，sh 的 printf 写不进封掉的号码，话收回来是空的，退出码也不是 0。咱们复原后再跑，绿。

## 验收

你在仓库根跑构建与测试：

```bash
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

成功条件你看汇总行：以 `100% tests passed` 开头、退出码 0。spawn 判例编在 `visor.tests` 里，第四十一章的换向与交叉判例也在其中，缺 /bin/sh 的环境它响亮跳过，未接线两查照跑。

整串验收咱们一条命令跑完：

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh --full
```

脚本有两个道次，不带参数是快道，只构建加静态检查，判例等 `--full` 才跑，一批收工以全量道为准。全量道六样照旧：普通构建、ASan/UBSan、TSan 各跑一遍全部测试，clang-format 与 clang-tidy 各扫一遍，fontgen 独立构建与测试收尾。咱们本轮从头配起跑全量道，实测三道测试用时 rel 135.59 秒、ASan/UBSan 135.46 秒、TSan 134.62 秒，四个测试目标全过，fontgen 两个目标 0.06 秒，末行打印：

```console
acceptance: all green (full lane)
```

你看末行的 full lane 字样，六道全绿。两根探针的亲手做法在上面一节。

## 口子

咱们把没做的照实记下。注释里设想的桥要把 socket 递给自己生养的程序，x11 与 wayland 的桌面窗口都还没接 spawn，判例自己填表自己跑。真消费者出现时，槽位、词汇、姿态、判据都在位。主线照待办走真 SPI 彩屏，第四口眼下由判例守着。

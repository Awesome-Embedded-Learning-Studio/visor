# 第三十章 贪吃蛇：固定步长与格子脏区

第二十九章的帧循环推进的是连续相位，这一章我们换个节奏，让动画一格一跳。`examples/snake` 是 480×320 的 `VISOR // SNAKE`：30×20 的格子，蛇每 160 毫秒走一步，吃食物变长、提速，方向键转向，Space 暂停，Enter 重开，I 反相。示例还带 `--demo` 自动巡演、固定种子快照和离屏自测，咱们一件件过。

## 先看结果

构建与运行命令跟上一章同款，你敲一遍：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
DISPLAY=:0 ./build/examples/snake
```

窗口按两倍显示为 960×640，逻辑画布 480×320。你会看到中间是 30×20 的棋盘，每格 12 像素，每两格有一个点，空着的棋盘也能读出尺度。初始蛇三格，食物随机。方向键转向，Space 暂停／继续，Enter 在结束后重开，I 在夜黑与纸白之间反相。标题右侧是分数、长度与速度，玩几局就能对上号。

`--snapshot` 不需要显示服务，咱们拿它出固定画面：

```bash
./build/examples/snake --snapshot build/visor-snake.pbm --demo --at-ms 30000
./build/examples/snake --snapshot build/visor-snake-paper.pbm --demo --at-ms 30000 --paper
./build/examples/snake --self-test
```

`--demo` 让一条贪心 AI 自动玩，死了 1.2 秒后自动重开。`--at-ms` 按固定 16 毫秒节拍推进，同一毫秒数出同一张图。示例默认种子是 `0x5EED`，你可以用 `--seed` 换一局食物序列。重开时种子会前进一格，所以同一进程里的第二局不会重复第一局的食物。

## 60 Hz 的帧，六步出头的蛇

我们把主循环和面板的 `tick` 分开对待。主循环和第二十九章一样，按宿主时钟的 `dt` 跑，15 毫秒一帧，`dt` 夹到 100 毫秒。区别在面板的 `tick`，`dt` 到了这里不直接变成移动，`tick` 把它攒进一个累加器，攒够一个步长才走一步：

```cpp
void SnakePanel::tick(std::uint64_t dt_ms) noexcept {
    if (demo_) {
        advance_demo(dt_ms);
    }
    if (game_.state() != SnakeGame::State::Running) {
        return;
    }
    accumulator_ += dt_ms;
    const std::uint64_t interval = game_.step_interval_ms();
    int steps = 0;
    while (accumulator_ >= interval && steps < 4) {
        accumulator_ -= interval;
        if (demo_) {
            game_.turn(choose_demo_direction());
        }
        static_cast<void>(game_.step());
        ++steps;
        const bool status_changed = game_.score() != last_score_ ||
                                    game_.length() != last_length_ ||
                                    game_.speed_level() != last_speed_;
        last_score_ = game_.score();
        last_length_ = game_.length();
        last_speed_ = game_.speed_level();
        invalidate(kBoardFrame);
        if (status_changed) {
            invalidate(kStatusBounds);
        }
        if (game_.state() != SnakeGame::State::Running) {
            invalidate(kBoardFrame);
            break;
        }
    }
    if (steps == 4) {
        accumulator_ = 0; // a long stall owes one step, not a teleport
    }
}
```

我们把节奏定在三个数上：初始步长 160 毫秒，也就是大约每秒 6.25 步。速度等级 `1 + 吃到的食物数 / 5`，步长 `160 - (等级 - 1) × 15`，最低 70 毫秒。食物越吃越多，蛇越走越快。输入始终按 60 Hz 的帧采样，急敲的转向由下一节的两格队列接住，存到走步那一刻消费，提速之后连按两个直角也丢不了。

`steps < 4` 和 `dt` 的 100 毫秒夹取，是我们在同一件事上留的两道保险。窗口被拖住或机器卡一下，累加器最多补四步，剩下的直接清掉，不把一次长停顿变成蛇的瞬移。暂停时 `tick` 在累加器之前就返回，键盘仍然每帧被泵进来。继续时从暂停前的余数接着走，不补暂停期间的时间。

## 两格转向缓冲

假如只有一个 `direction_`，会出什么事？咱们拿两组按键试。在两次移动之间快速按「上、左」，第二次会把第一次覆盖掉，蛇只转一次。更糟的是按了「左」再按「上」，要是直接写进方向，下一步就是 180 度掉头，走进自己的身体。`SnakeGame` 用一个两格的队列把转向存起来：

```cpp
void SnakeGame::turn(Direction direction) noexcept {
    if (state_ != State::Running && state_ != State::Paused) {
        return;
    }
    if (queued_count_ == 2) {
        return;
    }
    const Direction reference =
        queued_count_ > 0 ? queued_[static_cast<std::size_t>(queued_count_ - 1)] : direction_;
    if (direction == reference || is_opposite(direction, reference)) {
        return;
    }
    queued_[static_cast<std::size_t>(queued_count_)] = direction;
    ++queued_count_;
}
```

参考方向值得你多停半拍：它取的是队列里最后一个方向，不是当前方向。这样按「上、左」，队列里依次是上和左，而「上、下」里的下与队尾的上相反，直接丢掉。每一步消费队首：

```cpp
if (queued_count_ > 0) {
    direction_ = queued_[0];
    for (int i = 1; i < queued_count_; ++i) {
        queued_[static_cast<std::size_t>(i - 1)] = queued_[static_cast<std::size_t>(i)];
    }
    --queued_count_;
}
```

我们把队列定成两格，两次移动之间，人类最多转两次直角。两格是固定数组，不分配，溢出就丢新来的。判例把三种情况都写了，反向的「左」被忽略，「上、下」里的下被忽略，「上、左」两步分别走出上、左两个直角。

## 规则对象不发像素

我们把 `SnakeGame` 当纯规则对象来读。它不带绘制能力，身体、食物、方向、队列、分数、状态都在里面，`step()` 是唯一推进模拟的人，`on_draw` 只读它。我们沿用的线和第二十九章的 `Scope::tick` 是同一条，都要求绘制可重放：条带重放会把同一棵树画很多遍，规则对象只要保证同样的状态画同样的像素就够了。

碰撞判定里有一处容易写错的边界，我们在这里多停一步。蛇头走进**当前尾巴**所在的格子，在没吃到食物时是合法的，因为尾巴会在同一步腾出来。吃到食物时尾巴不腾，蛇头再走进尾巴所在的格子，才算碰到自己。`safe_for_next` 把它写在一处：

```cpp
bool SnakeGame::safe_for_next(Cell cell) const noexcept {
    if (!inside(cell)) {
        return false;
    }
    const bool eating = cell == food_;
    const int count = eating ? length_ : length_ - 1;
    for (int i = 0; i < count; ++i) {
        if (body_[static_cast<std::size_t>(i)] == cell) {
            return false;
        }
    }
    return true;
}
```

顺着 `step` 往下读，我们能对上每个分支。它一进来就算 `eating`，再拿 `safe_for_next` 判定下一步安不安全。吃到了就整条身体后移一格、长度加一、分数加十、重新放食物。没吃到就后移一格、丢掉尾巴。放食物按随机起点扫一遍空格，扫不到就置 `Won`，棋盘满了。

我们把面板的绘制分成三块：页眉标题与三行读数、棋盘、页脚提示。棋盘上，边框和每两格一个的格点打底，食物十字、身体方块与蛇头的方向缺口盖在上面。蛇身方块都是 10×10，留出 2 像素的格缝，格点让空棋盘有尺度。

## 一格一跳的脏区

我们把每一步欠的画收窄到棋盘，`tick` 在每一步 `invalidate(kBoardFrame)`。棋盘是 364×244 的框，16 行条带下跨 17 条。在申报这一层，一步只报棋盘，页眉与分数区都不在其中，吃到食物时 `score`、`length`、`speed_level` 有一个变了，才额外申报 `kStatusBounds`，让分数、长度、速度更新。到了重放这一层就没有这么干净，条带是横向整条，凡与棋盘同住一条带的邻居都会被顺带重画，页脚搭在最后一条带上，页眉横线和分数区贴着棋盘顶的几行也一样。它们不欠画，重画完还是原样。

自测把这几件事分开验，咱们一条条数：

- 就绪状态 `tick(10)` 一像素也不欠。
- 开始游戏后 `tick(10)` 短于步长，仍然一像素也不欠。
- 走一步会申报棋盘，但**不**申报页眉横线（`kHeaderRule` 在棋盘之外）。
- 吃一口会同时申报棋盘和分数区。

咱们估一估重画的代价。棋盘一次重画是十几条带，按每秒六到十四步算，代价比每秒 60 帧的整屏重画低得多。页眉标题在两步之间不动，分数只在吃到东西时动。格子脏区并不是「只重画两个格子」，条带模型仍然是横向整条重画，但把「哪一块欠画」缩小到棋盘，已经是这一章要讲的那一步。

## 种子、食物与自动演示

食物用 xorshift 生成随机起点，再从棋盘扫空格。`--seed` 决定起点序列，同一颗种子出同一局。咱们拿 `--snapshot --demo --at-ms 30000` 实际出过图，深色主题下是一局长 18、得分 150、速度 4 的自动演示，纸白主题同一时刻只换极性。

`--demo` 的 AI 很朴素，我们把它的选法摆出来。四个候选方向里跳过反向和不安全格，选曼哈顿距离食物最近的那个。没有安全方向就保持当前方向，死了由 1.2 秒后的自动重开接住。它会把自己困死，算不上无敌，但能稳定跑出一局有看头的分数。它还顺带说明，没有鼠标、没有玩家输入，另一个 `tick` 也能驱动规则对象，离屏自测立足的正是这一点。

`--self-test` 是这一章的离屏验收，不需要 X，你在只有终端的机器上也能跑：

```bash
./build/examples/snake --self-test
```

它从规则验起，我们跟着走一遍。新局三格、分数 0。走一步向东。放食物在头前，再走一步长到四格、得 10 分。反向被忽略。「上、左」两步各转一个直角。暂停不走。蛇头走进东墙，局结束。`reset` 回到开局。接着验帧路径，整幅对 16 行条带逐像素一致，就绪和短步长零欠画，按 Space 开始，走一步欠棋盘不欠页眉，吃一口欠棋盘与分数区。当前自测报告 1456 个棋盘像素在开始与走一步之间变化。

## 判例与探针实录

这一章的代码第一次过 `clang-tidy` 时红了五类，我们一类一类记下来。`base` 命名空间别名没用上。`if (level > 7)` 被要求写 `std::min`。`inside` 不碰成员，被要求改成静态。多行 `RectI` 初始化缺尾逗号。一个不带参数的 lambda 被要求去掉括号。全部按检查修正，没有关检查。

队列的判例红过一回，值得咱们记一笔。头一版只验了「上、下」里的下被丢掉，没有验「上、左」真的能连着走两个直角。我们补上第一段「first queued turn goes up / second queued turn goes left」之后，队列才真正被两格覆盖。

两根红绿探针，你可以自己动手做：

- 把 `tick` 里的 `invalidate(kBoardFrame)` 注掉，自测会在「a step does not owe the board」红，窗口里蛇也会停止更新。复原再绿。
- 把 `turn` 的队列上限改成 0（或只留当前方向），「second queued turn goes left」判例会红：第二个转向被丢掉，蛇继续向上。复原再绿。

我们肉眼检查了就绪页、8 秒、20 秒、30 秒的夜黑快照与 30 秒纸白快照，格点、蛇身、蛇头缺口、食物十字、页眉三行、页脚提示都在位。Xvfb 下 `--frames 120` 的真窗口冒烟退出码 0。真窗口的手感与关闭，仍由作者在桌面上亲手走。

## 验收

```bash
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

脚本覆盖六道检查，我们一道道清点，普通构建与全部测试、ASan/UBSan、TSan、clang-format、clang-tidy，外加 fontgen 独立构建。CTest 现在挂着三项：

- `visor.tests`
- `visor.signal.selftest`
- `visor.snake.selftest`

我们跑了一轮，六道全部通过。rel 133.54 秒，加 signal 0.01 秒、snake 0.00 秒。ASan/UBSan 134.30 秒，加 0.04 / 0.03 秒。TSan 134.08 秒，加 0.05 / 0.04 秒。格式、静态检查与 fontgen 两组全过，脚本末行打印 `acceptance: all green`。

## 下一批的口子

真窗口冒烟仍由作者亲手完成，之后我们打算看 X11 的提交策略，把脏区矩形对到 X 的提交矩形上。游戏本身还有空间：更高等级的加速、障碍物、双人对战、存档的最高分。我们刻意只做了一局可重开的单蛇，把固定步长、输入缓冲和棋盘脏区讲清楚。示例没有改核心，`SnakeGame` 住在 `examples/snake.cpp` 里。如果以后第二个游戏也要同一套累加器和转向缓冲，我们再考虑把这两件抽到 `ui` 或 `base`。

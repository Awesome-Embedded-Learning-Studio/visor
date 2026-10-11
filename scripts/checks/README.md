# 必跑验收

每批最终验收必须运行这一条命令：

```sh
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

覆盖六道：rel 构建与全部测试，ASan/UBSan 构建与全部测试（泄漏检查开启，UB 触发即中断），TSan 构建与全部测试（数据竞争触发即中断，`VISOR_TSAN` 与 `VISOR_SANITIZE` 互斥，分目录构建），clang-format 格式检查，clang-tidy 静态检查，fontgen 独立构建与测试。构建目录复用 `build`、`build-asan`、`build-tsan`、`build-fontgen`，重复运行是幂等的。

任何一步失败脚本立即停止。不得关检查、忽略退出码或跳过失败用例后报「验收通过」。覆盖范围与限制要跟结果一起记录下来。TSan 这一道面对的测试当前是单线程，它此刻是铺路与回归网，等第一个线程化宿主进场时开始抓真问题。

## 单项手动跑

验收脚本就是下面这些命令的串联，想单独跑某一到时用（`visor.check-format`、`visor.check-tidy`、`visor.docs` 三个目标只在装了对应工具的构建目录里存在）：

```bash
ctest --test-dir build --output-on-failure        # rel 全部测试
cmake --build build --target visor.check-format   # 格式检查
cmake --build build --target visor.check-tidy     # 静态检查
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_SANITIZE=ON
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure   # ASan/UBSan
cmake -S tools/fontgen -B build-fontgen -G Ninja && cmake --build build-fontgen && ctest --test-dir build-fontgen   # 取模工具独立构建
cmake --build build --target visor.docs           # API 文档到 build/api/（不在验收六道里）
```

# 必跑验收

每批最终验收必须运行这一条命令：

```sh
VISOR_CHECK_JOBS=4 scripts/checks/acceptance.sh
```

覆盖六道：rel 构建与全部测试，ASan/UBSan 构建与全部测试（泄漏检查开启，UB 触发即中断），TSan 构建与全部测试（数据竞争触发即中断，`VISOR_TSAN` 与 `VISOR_SANITIZE` 互斥，分目录构建），clang-format 22.1.8 格式检查，clang-tidy 22.1.8 静态检查，fontgen 独立构建与测试。构建目录复用 `build`、`build-asan`、`build-tsan`、`build-fontgen`，重复运行是幂等的。

任何一步失败脚本立即停止。不得关检查、忽略退出码或跳过失败用例后报「验收通过」。覆盖范围与限制要跟结果一起写进当批批记。TSan 道自 B2 起在岗：当前测试单线程，它此刻是铺路与回归网，等第一个线程化宿主进场时开始抓真问题。

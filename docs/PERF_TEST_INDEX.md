# C++ 框架性能测试总览

> 六类测试的索引与边界说明。方案/边界见各 `PERF_TEST_0x_*.md`。
>
> **唯一汇总入口：** [../reports/C++框架性能测试综合报告.md](../reports/C++框架性能测试综合报告.md)  
> （基线 + 优化轮同文；旧的按日期拆分文件仅保留跳转。）

## 测试清单

| # | 名称 | 范围（起 → 止） | 文档 | 状态 |
|---|------|-----------------|------|------|
| 1 | **多任务执行延时** | 程序**收到可执行任务** → 策略运行 → **任务结束** | [PERF_TEST_01_TASK_EXECUTION_LATENCY.md](./PERF_TEST_01_TASK_EXECUTION_LATENCY.md) | ✅ 基线 `20260827_030418`；✅ 优化轮 `20260902_062341` / `065439`；⏳ sticky pool 待正式计量 |
| 2 | **多任务调度延时** | 任务**写入数据库** → Worker/Executor **认领任务** | [PERF_TEST_02_TASK_SCHEDULING_LATENCY.md](./PERF_TEST_02_TASK_SCHEDULING_LATENCY.md) | ✅ 同 Test 1；parallel=3 后 status2→claim 中位 ~110 ms |
| 3 | **行情延时** | **订阅行情** → 首条/后续 push 到达；push **间隔** | [PERF_TEST_03_MARKET_DATA_LATENCY.md](./PERF_TEST_03_MARKET_DATA_LATENCY.md) | ✅ 扩量完成（50标的热连接+冷附录，`20260831_085227`） |
| 4 | **Python 调用延时** | 公式计算、Telegram 等 **Python 侧调用** | [PERF_TEST_04_PYTHON_CALL_LATENCY.md](./PERF_TEST_04_PYTHON_CALL_LATENCY.md) | ✅ 公式+TG（`082540` + `20260901_034336`） |
| 5 | **API 包装延时** | OpenD **裸 API** vs **程序封装后** 返回耗时对比 | [PERF_TEST_05_API_WRAPPER_LATENCY.md](./PERF_TEST_05_API_WRAPPER_LATENCY.md) | ✅ place + query/cancel（`082629` / `20260901_080751`） |
| 6 | **稳定性长跑** | 长周期 **模拟盘** 任务连续运行（泄漏、卡顿、失败率） | [PERF_TEST_06_STABILITY_SOAK.md](./PERF_TEST_06_STABILITY_SOAK.md) | ✅ **结论：24h 连续运行可行** |

---

## 全链路位置（六类测试如何拼接）

```
② 调度延时（Test 2）  insert → Worker prepare → status=2 → Executor claim
                ↓
① 执行延时（Test 1）  派发 →（pool 换订阅 / 或新进程 Init）→ 判断 → 报单 → 成交
         │                              │
    ③ 行情延时（Test 3）          ⑤ API 包装（Test 5）
         │
    ④ Python 调用（Test 4）

⑥ 稳定性长跑（Test 6）— hours/days 尺度
```

---

## 执行侧架构（优化后）

| 项 | 说明 |
| --- | --- |
| sticky pool | N=`parallel` 个 `strategy_cpp --pool-worker`；任务经 Unix socket 派发；Init 一次，任务间换订阅 |
| Connect flock | `/tmp/moomoo_opend_connect.lock` |
| `-late` / hot path | `MOOMOO_BENCH_HOT_PATH=1` 跳过 Connect/Place 200 ms sleep |
| Gateway | `ORDER_BROKER_MODE=gateway_fallback`，C++ `:18766` |
| 关池 | `MOOMOO_STRATEGY_POOL=0` |

---

## 共用基础设施

| 组件 | 路径 |
|------|------|
| 延时 CSV | `strategy/task_system/bench_logs/task_latency.csv` |
| 统计脚本 | `bench/task_latency_analyze.py` / `bench/task_latency_stats.py` |
| 批量 bench | `bench/task_latency_batch_submit.py` |
| Pool IPC | `src/task/strategy_worker_pool.*` |

```bash
cd moomoo_python/strategy/task_system/script && ./worker.sh restart -late
cd /home/ubuntu/workspace/moomoo_CPP && ./run_executor.sh restart -n 4 -late -gateway
```

---

## 环境与约定

- OpenD paper `127.0.0.1:11121`；`nana_paper`；`f_task_type=4`
- 正式数字以综合报告 CSV 重算为准；自动 `task_latency_*.md` 仅追溯
- 差数据：负延时、段两端极值不进入中位数结论

---

## 文档更新规则

1. **只维护一份** `reports/C++框架性能测试综合报告.md`。
2. 新一轮测试：更新该报告对应章节 + 本索引状态表。
3. 原始 CSV / 自动 md 仍保留作追溯。

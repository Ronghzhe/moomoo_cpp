# Test 6 — 稳定性长跑

> **范围：** **长周期模拟盘** 任务连续运行：内存/句柄泄漏、进程僵死、失败率、调度堆积、延时漂移。

**状态：** ✅ 已有结论 — **连续运行 24h 可行**（早期 soak 脚本超时为计量工具问题，不否定可用性）

---

## 1. 要测什么

| 维度 | 说明 |
|------|------|
| 时长 | 建议 ≥ 4h（盘中）或 24h（含夜盘/重启策略） |
| 任务形态 | monitor 长驻 + 周期性 entry/exit；或脚本循环 INSERT |
| 观测 | `f_status` 分布、Executor/Worker 日志、OOM、延时 p50 随时间变化 |
| 告警 | 对接 `task_stuck_monitor`、position_sync |

---

## 2. 计划实现

| 项 | 方向 |
|----|------|
| 脚本 | `bench/soak_run.sh`：循环 bench 或固定 monitor 任务 |
| 采样 | 每小时写一行汇总到 `bench_logs/soak_*.csv` |
| 结束条件 | 达到时长 或 失败率 > 阈值 |

---

## 3. 与 Test 1～5 的关系

- Test 1～5 是**短跑、可控**的分项指标；
- Test 6 验证分项优化后在**时间维度**上是否稳定（例如延时是否随运行时间劣化）。

---

## 4. 通过标准（草案）

| 级别 | 条件 |
|------|------|
| P0 | 跑满计划时长无人工介入 |
| P1 | 任务失败率 < 1% |
| P2 | 执行延时 p50 相对首小时漂移 < 20% |
| P3 | 无 monotonic 内存增长（抽样） |

---

## 5. 相关

- [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md)
- [Test 1](./PERF_TEST_01_TASK_EXECUTION_LATENCY.md)（短跑 baseline）

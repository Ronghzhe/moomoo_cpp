# Test 2 — 多任务调度延时

> **范围：** 任务 **`INSERT` 进数据库（`f_status=1`）** 起，到 **Worker prepare 完成 / Executor 认领（`f_status=2`）** 止。  
> **不含：** strategy 进程内执行 —— 见 [Test 1](./PERF_TEST_01_TASK_EXECUTION_LATENCY.md)。

**状态：** ✅ 首轮（与 Test 1 同批 bench `20260827_030418`）；Worker finalize **已拆关键/非关键路径**（2026-08-27）

---

## 1. 要测什么

多任务依次或并发入库时，**调度链路**各节点延时：Worker 是否及时 prepare、Executor 是否及时 claim、是否存在排队与 race。

---

## 2. 环节定义

```
insert_ts_ms          脚本/业务写入 DB
        │
        ▼
prepare_acquire_ms    Worker 认领 f_status=1
        │
        ▼
prepare_done_ms       build mapped 完成
        │
        ▼
prepare_status_2_ms   写库 f_status=2
        │
        ▼
executor_claim_ms     Executor 选中该任务
        │
        ▼
mapped_ready_ms       Executor 确认 mapped 文件可读
```

### 派生指标

| 字段 | 含义 |
|------|------|
| `ms_insert_to_ready` | 入库 → Worker 置 2 |
| `ms_ready_to_executor` | 置 2 → Executor claim（**负值表示 claim 早于写库完成，属 race**） |
| `ms_executor_to_shell_end` | claim → shell 结束（含执行段，分析调度时仅取 claim 前段） |

---

## 3. 计划测试场景

| 场景 | 说明 |
|------|------|
| 顺序单 Worker | 当前 bench 默认 |
| Worker poll 间隔 | 改 `--poll-sec` 观察 `insert→acquire` |
| 多 Executor 并行 | `-n 4` 下 claim 竞争 |
| 入库 burst | 短间隔连续 INSERT 多条 |

---

## 4. 怎么跑（复用 Test 1 bench）

与 [Test 1](./PERF_TEST_01_TASK_EXECUTION_LATENCY.md) 相同脚本；分析时**只取调度段字段**。

```bash
python3 bench/task_latency_batch_submit.py
python3 bench/task_latency_analyze.py --batch <batch_id>
```

Worker 必须 `-late` 才有 prepare 段打点。

### 5.0 Worker finalize 优化（2026-08-27）

`task_worker_main.py` 将 prepare 收尾拆为：

| 路径 | 内容 | 阻塞 Executor？ |
|------|------|----------------|
| **同步（关键）** | `UPDATE strategy_tasks` → `f_status=2`（+ 必要时 `f_task_params`） | 是 — 完成后 Executor 即可 SELECT |
| **异步（非关键）** | `strategy_logs` / `strategy_task_events` / Telegram 队列通知 | 否 — 线程池后台执行 |

`prepare_status_2_ms` 在 **status=2 写入后立即** 打点（不再等 TG）。

调试时可 `./worker.sh restart -late --sync-finalize-side-effects` 恢复旧同步行为。

**仍未异步：** Executor 侧 `poll_sec=2` 轮询；C++ claim 时的 audit log 仍同步（可另做优化）。

---

## 5. 最新结果（batch `20260827_030418`）

**统计口径：** `status=2 → Executor 认领` 排除 **负延时**（staging race）及 **缺少 executor_claim_ms** 的样本；其余调度段 n=12。

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 入库 → Worker 认领 | 12 | 589.1ms | 515.5ms | 127937 | 357.7ms | 118ms | 1260ms |
| 认领 → mapped 完成 | 12 | 1.7ms | 1.5ms | 0.8 | 0.9ms | 1ms | 4ms |
| mapped → 写库 status=2 | 12 | 685.6ms | 679.0ms | 296 | 17.2ms | 671ms | 731ms |
| status=2 → Executor 认领 | **7** | 415.4ms | **602.0ms** | 98715 | 314.2ms | **47ms** | 753ms |
| 入库 → ready（派生） | 12 | 1276.3ms | 1221.5ms | 130036 | 360.6ms | 807ms | 1970ms |
| ready → Executor 认领（派生） | **7** | 415.4ms | **602.0ms** | 98715 | 314.2ms | 47ms | 753ms |

**已排除 5 条（共 12 leg 中）：**

| task | leg | f_id | 原因 |
|------|-----|-----:|------|
| bench_hk_02208_..._001 | entry | 4758 | ms_ready_to_executor=-376ms（race） |
| bench_hk_02208_..._001 | exit | 4758 | 缺少 executor_claim_ms |
| bench_hk_03308_..._002 | entry | 4759 | ms_ready_to_executor=-157ms（race） |
| bench_hk_02208_..._005 | entry | 4762 | ms_ready_to_executor=-3ms（race） |
| bench_hk_03308_..._006 | entry | 4763 | ms_ready_to_executor=-214ms（race） |

**为何会出现负值：** Executor 从 DB 看到 `f_status=2` 并写入 `executor_claim_ms` 时，Worker 的 `prepare_status_2_ms` 尚未 merge 进 staging JSON — 不是 Executor 真的提前认领，而是 **异步 race**，该样本不可用于「status=2 → claim」统计。

**解读（排除后）：**

- **Worker 段**：`insert→acquire` 中位数 ~516ms，标准差 ~358ms。
- **mapped→status=2**：中位数 ~679ms，极稳。
- **Executor 认领（有效 n=7）**：中位数 **602ms**（47ms～753ms），比排除前「80ms 中位数」更可信；此前低中位数是被 race 负值拉偏。
- **端到端调度**：`insert→ready` 中位数 ~1.22s，不受 Executor 段排除影响（n 仍为 12）。

完整报告：`reports/task_latency_stats_20260827_030418.md`

## 6. 通过标准（建议，待验证）

| 级别 | 条件 |
|------|------|
| P0 | `prepare_acquire_ms`、`prepare_status_2_ms`、`executor_claim_ms` 均有值 |
| P1 | `ms_insert_to_ready` p50 < 3s（单 Worker，paper） |
| P2 | `ms_ready_to_executor` p50 ≥ 0 且 p50 < 2s（race 修正后统计） |

---

## 7. 相关

- [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md)

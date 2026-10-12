# Task Latency Bench

## 输出

| 文件 | 说明 |
|------|------|
| `strategy/task_system/bench_logs/staging/<f_id>_<task>.json` | 各进程异步更新的暂存 |
| `strategy/task_system/bench_logs/task_latency.csv` | **一行一 leg** 的延时宽表（同 f_id 可有两行：entry + exit） |
| `reports/task_latency_<batch_id>.md` | 批次汇总 |
| `reports/task_latency_stats_<batch_id>.md` | 同上（`task_latency_analyze.py` 默认输出名） |

## 延时统计（均值 / 中位数 / 方差 / 标准差）

bench 结束后会自动生成统计报告；也可对历史 CSV 单独分析：

```bash
cd /home/ubuntu/workspace/moomoo_CPP
python3 bench/task_latency_analyze.py --batch 20260827_030418
python3 bench/task_latency_analyze.py --batch 20260827_030418 --json   # 输出 JSON 摘要
```

报告分 **Test 2 调度**、**Test 1 entry**、**Test 1 exit** 三节，每指标含 n / 均值 / 中位数 / 方差 / 标准差 / 最小 / 最大。

## 批量插入（港股，3 轮开平，逐步等待，方案 B）

每个 symbol 每轮 **1 条 DB 任务**：先 `entry_now` 开仓，完成后 **UPDATE 同任务** 为 `exit_now` 平仓（与 Web 手动平仓相同，Worker merge 旧 mapped 保留 `opened_qty`）。

| symbol | qty | 每轮 |
|--------|-----|------|
| 02208 | 200 | entry → requeue exit |
| 03308 | 50  | entry → requeue exit |

合计 **6 条 DB 任务**（3 rounds × 2 symbols）；CSV 最多 **12 行**（每任务 entry + exit 各一行）。

```bash
cd /home/ubuntu/workspace/moomoo_CPP
python3 bench/task_latency_batch_submit.py
```

仅插入 entry、不 requeue exit（不推荐测平仓）:

```bash
python3 bench/task_latency_batch_submit.py --no-wait
```

## 批量插入（美股模拟盘，2 轮开平）

港股休盘时可改用美股模拟盘测延时。每个 symbol 每轮 **1 条 DB 任务**：先 `entry_now` 开仓 1 股，完成后 **UPDATE 同任务** 为 `exit_now` 平仓。

| symbol | qty | 每轮 |
|--------|-----|------|
| AAPL   | 1   | entry → requeue exit |
| MU     | 1   | entry → requeue exit |
| SMCI   | 1   | entry → requeue exit |

默认 **2 轮** → 共 **6 条 DB 任务**；CSV 最多 **12 行**。

```bash
cd /home/ubuntu/workspace/moomoo_CPP
python3 bench/submit_us_paper_tasks.py
```

仅插入 entry、不 requeue exit:

```bash
python3 bench/submit_us_paper_tasks.py --no-wait
```

覆盖轮数（例如 3 轮）:

```bash
python3 bench/submit_us_paper_tasks.py --rounds 3
```

也可直接指定配置:

```bash
python3 bench/task_latency_batch_submit.py --config bench/config/task_bench_us_paper.json
```

## 前提（需手动）

1. **重新编译** C++（埋点在 C++ 侧）
2. **Worker 延时打点**（prepare 阶段）:
   ```bash
   cd moomoo_python/strategy/task_system/script
   ./worker.sh restart -late    # 开
   ./worker.sh restart          # 关（默认）
   ```
   开启后 **仅 bench 脚本已建 staging 的任务** 才会写 prepare 时间点
3. **Restart C++ Executor type=4**（bench 关 open_stagger 节流 + 跳过 Place 200ms sleep）:
   ```bash
   cd moomoo_CPP/strategy/task_system/script
   ./executor.sh restart -n 4 -late    # bench：无 stagger sleep
   ./executor.sh restart -n 4           # 生产默认（2s x 20 buckets）
   ```

本脚本 **不会** 启动/重启任何 daemon。

## 流程

**Entry:** `INSERT f_status=1` → Worker prepare → `f_status=2` → Executor → strategy_cpp 开仓 → `f_status=3`

**Exit（同 f_id）:** `UPDATE f_task_params=exit_now, f_status=1` → Worker prepare（merge mapped 持仓）→ `f_status=2` → Executor 平仓 → `f_status=3`

每条 leg 各写 CSV 一行。

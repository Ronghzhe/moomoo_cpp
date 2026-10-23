# Test 1 — 多任务执行延时

> **范围：** 程序**已经拿到可执行任务**（`f_status=2`，mapped 已就绪）之后，到**本 leg 执行结束**（strategy 进程退出、Executor 收尾）的全链路延时。  
> **不含：** 任务入库、Worker prepare、Executor 认领 —— 见 [Test 2](./PERF_TEST_02_TASK_SCHEDULING_LATENCY.md)。

---

## 1. 要测什么

在**多任务顺序/交替**执行时，从「Executor 开始跑 strategy」到「本 leg 完成」，每个环节耗多少。

典型 leg：**开仓（entry_now）** 或 **平仓（exit_now，同任务 requeue）**。

---

## 2. 环节定义

```
Executor claim / shell_start
        │
        ▼
┌───────────────────┐
│ A. 策略进程启动    │  strategy_main_start → config_loaded
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ B. OpenD 连接     │  opend_connected → subscribe_done → order_client_ready
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ C. 行情与判断      │  first_book_push → first_judge → entry/exit_judge_hit
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ D. 报单与成交      │  place_start → SDK API → sent → push → filled
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ E. 退出            │  strategy_exit → shell_end → executor_finalize
└───────────────────┘
```

### 2.1 打点字段对照

| 环节 | 时间戳字段 | 派生字段（推荐看） |
|------|------------|-------------------|
| A 启动 | `strategy_main_start_ms`, `config_loaded_ms` | — |
| B OpenD | `opend_connected_ms`, `subscribe_done_ms`, `order_client_ready_ms` | — |
| C 行情/判断 | `first_book_push_ms`, `first_judge_ms`, `entry_judge_hit_ms` / `exit_judge_hit_ms` | `ms_insert_to_first_push`（含调度时需从 insert 起算） |
| D 报单 | `entry_place_*` / `exit_place_*`, `*_filled_ms` | `entry_place_api_ms`, `us_hit_to_place_sent`, `us_place_sent_to_filled`, `ms_entry_hit_to_filled` |
| E 收尾 | `strategy_exit_ms`, `shell_end_ms` | `ms_shell_start_to_end`, `ms_insert_to_done` |

报单细分（C++ 内）：

| 字段 | 含义 |
|------|------|
| `entry_place_pre_us` | 报单前准备（微秒） |
| `entry_place_sdk_us` | OpenD SDK 调用（微秒） |
| `entry_place_post_us` | 报单后处理（微秒） |
| `entry_fill_source` | 成交来源（`push` / `poll`） |

### 2.2 端到端 pipeline 时间戳（每 leg 各阶段首次）

| 阶段 | 字段 | 说明 |
|------|------|------|
| 行情进入 OpenD | `md_opend_recv_ms` | OpenD 推送里的服务端接收时间（非本机 wall，可与本地戳对比 skew） |
| SDK 收到 | `md_sdk_recv_ms` | `OnPush_UpdateOrderBook` 入口 |
| Protobuf 解码完成 | `md_proto_done_ms` | 写入 orderbook cache 后 |
| 回调触发 | `md_callback_ms` | `NotifyBookPush` 后 |
| 策略线程唤醒 | `md_loop_wakeup_ms` | `WaitBookPush` 返回后 |
| 策略计算 | `entry_pipe_strategy_ms` / `exit_pipe_strategy_ms` | 首次 entry/exit judge hit |
| 风控通过 | `entry_pipe_risk_ms` / `exit_pipe_risk_ms` | 可下单前（tradability/book/qty 等已通过） |
| 发出订单 | `entry_pipe_order_sent_ms` | 与 `entry_place_sent_ms` 同刻 |
| 订单确认 | `entry_pipe_order_ack_ms` | Place 成功返回（含 gateway HTTP 路径） |
| 成交/失败 | `entry_pipe_order_terminal_ms` + `entry_pipe_terminal_kind` | `fill` / `failed` |

派生段：`ms_md_sdk_to_proto`、`ms_md_proto_to_callback`、`ms_md_callback_to_loop`、`ms_entry_pipe_*` 等在 CSV 合并时自动计算。

---

## 3. 怎么跑

```bash
cd /home/ubuntu/workspace/moomoo_CPP

# 前提：Worker + Executor 均已 restart -late（见 PERF_TEST_INDEX.md）

python3 bench/task_latency_batch_submit.py
```

- **方案 B（当前）：** 默认 50 条 DB 任务，每条先 entry 再 **UPDATE requeue exit**（与 Web 手动平仓一致，mapped 保留持仓）。
- **标的：** HK.02208×200、HK.03308×50，各 25 轮 → **50 entry + 50 exit = 100 legs**。
- **覆盖：** `python3 bench/task_latency_batch_submit.py --rounds N`（腿数 ≈ N × 2 symbols × 2）。
- **输出：** `bench_logs/task_latency.csv`（一行一 leg）。
- **耗时预估：** paper 下约 15–30 分钟（串行开平）。

配置：`bench/config/task_bench_hk_paper.json`（`rounds: 25`）

**统计报告：**

```bash
python3 bench/task_latency_analyze.py --batch <batch_id>
```

---

## 4. 最新结果（batch `20260827_030418`）

**日期：** 2026-08-27 11:04（UTC+8）  
**结果：** 12/12 leg 成功（6 开 + 6 平），`final_status=3`  
**原始数据：** `strategy/task_system/bench_logs/task_latency.csv`（batch_id=`20260827_030418`）

### 4.1 一句话结论

- **C++ 报单路径约 45ms**（开/平稳定）。
- **端到端 shell 约 7s**，其中 **paper 等成交约 5s** 是主因，不是报单代码慢。
- **方案 B requeue 平仓** 6/6 成功，exit 报单打点完整。

### 4.2 执行段统计（batch `20260827_030418`，n=6）

**开仓（entry）**

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 6 | 6726.5ms | 7100.5ms | 3737569 | 1933.3ms | 3007ms | 8266ms |
| SDK 报单 API | 6 | 47.0ms | 47.5ms | 4.4 | 2.1ms | 43ms | 49ms |
| 触发 → 报单发出 | 6 | 47.3ms | 48.0ms | 5.1 | 2.3ms | 43ms | 49ms |
| 报单 → 成交 | 6 | 4199.5ms | 4666.5ms | 3236084 | 1798.9ms | 730ms | 5859ms |
| 触发 → 成交 | 6 | 4246.8ms | 4715.0ms | 3236765 | 1799.1ms | 777ms | 5908ms |

**平仓（exit，同任务 requeue）**

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 6 | 6532.5ms | 7290.5ms | 4169666 | 2042.0ms | 2445ms | 7919ms |
| SDK 报单 API | 6 | 42.8ms | 43.0ms | 15.0 | 3.9ms | 38ms | 47ms |
| 触发 → 报单发出 | 6 | 43.0ms | 43.0ms | 13.6 | 3.7ms | 38ms | 47ms |
| 报单 → 成交 | 6 | 4389.5ms | 5003.0ms | 2915002 | 1707.3ms | 1000ms | 5649ms |
| 触发 → 成交 | 6 | 4432.5ms | 5044.0ms | 2910231 | 1705.9ms | 1047ms | 5696ms |

**解读：**

- **C++ 报单路径**：均值 ~43–47ms，标准差 ~2–4ms，波动小，可视为稳定。
- **paper 等成交**：中位数 ~4.7–5.0s，但标准差 ~1.7–1.8s、方差极大（百万级 ms²），单笔 730ms～5.9s 均有，**不能只看均值**。
- **成交确认**：100% `fill_source=push`。

完整报告：`reports/task_latency_stats_20260827_030418.md`

### 4.3 shell 内部分解（典型一笔，f_id=4758 开仓）

| 子阶段 | 耗时 |
|--------|------|
| OpenD 连接 | ~0.9s |
| 订阅 → 首笔 book | ~1.5s |
| book → 触发判断 | ~10ms |
| 触发 → 报单发出 | ~49ms |
| 报单 → 成交 | ~5.0s |

平仓 leg（同 f_id requeue）：OpenD 连接 ~0.2s（快于开仓），报单→成交仍 ~5s。

### 4.4 同任务开 vs 平（节选）

| f_id | 标的 | 开仓 shell | 平仓 shell | 开仓 sent→fill | 平仓 sent→fill |
|------|------|------------|------------|----------------|----------------|
| 4758 | 02208 | 8.3s | 7.3s | 5.0s | 5.0s |
| 4762 | 02208 | 6.8s | **2.4s** | 4.2s | **1.0s** |

除 4762/4763 两笔外，sent→fill 均在 **4～5.6s**；paper 成交时间波动大，**以 p50≈5s 为准**。

### 4.5 本测试范围的已知限制

| 项 | 说明 |
|----|------|
| paper 成交 ~5s | 模拟盘特性，**不能**代表 real 成交速度 |
| 每 leg 重启 strategy | shell 含冷启动；未测长驻进程场景 |
| CSV 表头 | 旧表头 52 列 vs 新数据 76 列，Excel 直接打开会错位；分析用完整 schema |

---

## 5. 通过标准（建议）

| 级别 | 条件 |
|------|------|
| **P0** | entry + exit leg 均 `final_status=3`；`place_api_ms` 有值 |
| **P1** | 开仓/平仓 `place_api_ms` p50 < 100ms（paper） |
| **P2** | `fill_source=push` 占比 > 95% |
| **参考** | sent→fill 在 paper 下仅作记录，不作 hard gate |

Real 环境需单独复测并更新本节阈值。

---

## 6. 生产环境：仅 C++ 执行段（Worker 不开 `--task-latency`）

| 项 | 说明 |
|----|------|
| 开关 | **无需额外开关**：`trade_executor_cpp` 跑任务即写 staging；子进程 `strategy_cpp` 继承 `MOOMOO_TASK_LATENCY_*` |
| 全局 CSV | `strategy/task_system/bench_logs/task_latency.csv` |
| **品种目录 CSV** | `strategy/daily_logs/acc_{acc_id}_idx_{acc_index}/{HK_02208}/latency.csv`（与 `orders.csv` / `runtime.log` 同目录，每次 leg 追加一行） |
| 分析重点 | `ms_shell_start_to_end`、`entry/exit_place_*`、`entry_gateway_*_us`、`ms_*_hit_to_filled`；**勿用**空的 `insert_ts_ms` / `prepare_*` |
| 统计 CLI | `python3 bench/task_latency_analyze.py --days 7 --execution-only` |

---

## 7. 相关文件

| 文件 | 说明 |
|------|------|
| [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md) | 六项测试总览 |
| [../bench/README.md](../bench/README.md) | bench 脚本用法 |
| `bench/task_latency_analyze.py` | 延时统计 CLI |
| `reports/task_latency_stats_20260827_030418.md` | 该批次完整统计报告 |

---

## 8. 后续

- [ ] paper 多任务**并行**（当前为顺序等待）下的执行延时
- [ ] real 小仓位复测 Test 1
- [ ] 长驻 strategy 进程 vs 每 leg 重启的 shell 耗时对比（与 Test 6 交叉）

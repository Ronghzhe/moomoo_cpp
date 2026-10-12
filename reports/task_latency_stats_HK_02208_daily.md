# Task Latency 统计报告 `prod_365d`

- csv: `/home/ubuntu/workspace/moomoo_CPP/strategy/daily_logs/acc_0_idx_0/HK_02208/latency.csv`
- 样本行数: 4（entry 0 / exit 4）

统计说明：**均值**、**中位数**用于中心趋势；**方差**、**标准差**用于波动；us 字段在表中已换算为 ms。
范围：**仅 C++ 执行段**（Executor claim → strategy → 报单/成交）；Worker prepare / `insert_ts_ms` 未开启时调度列为空，请用 `shell_start_ms` 起算的指标。

## Test 1 — 执行延时 · 开仓（entry）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|

## Test 1 — 执行延时 · 平仓（exit）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 2 | 3.5ms | 3.5ms | 4.5 | 2.1ms | 2.0ms | 5.0ms |
| SDK 调用（us→ms） | 2 | 0.0ms | 0.0ms | 0.0 | 0.0ms | 0.0ms | 0.0ms |
| 触发 → 报单发出 | 2 | 1791787866.9ms | 1791787866.9ms | 62202.9 | 249.4ms | 1791787690.6ms | 1791788043.3ms |
| 报单 → 成交 | 2 | 1791787866.9ms | 1791787866.9ms | 62202.9 | 249.4ms | 1791787690.6ms | 1791788043.3ms |
| 触发 → 成交 | 2 | 243255.5ms | 243255.5ms | 414960.5 | 644.2ms | 242800.0ms | 243711.0ms |

## 逐条样本

- `bench_hk_02208_20261012_064800_001` leg=exit f_id=6103 sched_total=ms shell=ms api=1791787682713ms
- `bench_hk_02208_20261012_064800_001` leg=exit f_id=6103 sched_total=1791787689241ms shell=2ms api=1791787688776ms
- `bench_hk_02208_20261012_065353_001` leg=exit f_id=6104 sched_total=ms shell=ms api=1791788034929ms
- `bench_hk_02208_20261012_065353_001` leg=exit f_id=6104 sched_total=1791788041501ms shell=5ms api=1791788041037ms

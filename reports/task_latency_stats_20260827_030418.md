# Task Latency 统计报告 `20260827_030418`

- csv: `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/task_latency.csv`
- 样本行数: 12（entry 6 / exit 6）

统计说明：**均值**、**中位数**用于中心趋势；**方差**、**标准差**用于波动；us 字段在表中已换算为 ms。
Test 2 调度段：**排除负延时**（Executor claim 早于 staging 写 `prepare_status_2_ms` 的 race）及 **缺少 executor_claim_ms** 的样本。

## Test 2 — 调度延时（Worker + Executor 认领）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 入库 → Worker 认领 | 12 | 589.1ms | 515.5ms | 127937.0 | 357.7ms | 118.0ms | 1260.0ms |
| 认领 → mapped 完成 | 12 | 1.7ms | 1.5ms | 0.8 | 0.9ms | 1.0ms | 4.0ms |
| mapped → 写库 status=2 | 12 | 685.6ms | 679.0ms | 295.9 | 17.2ms | 671.0ms | 731.0ms |
| status=2 → Executor 认领 | 7 | 415.4ms | 602.0ms | 98715.0 | 314.2ms | 47.0ms | 753.0ms |
| 入库 → ready（派生） | 12 | 1276.3ms | 1221.5ms | 130036.1 | 360.6ms | 807.0ms | 1970.0ms |
| ready → Executor 认领（派生） | 7 | 415.4ms | 602.0ms | 98715.0 | 314.2ms | 47.0ms | 753.0ms |

### Test 2 已排除样本

| task | leg | f_id | 原因 |
|------|-----|-----:|------|
| `bench_hk_02208_20260827_030418_001` | entry | 4758 | ms_ready_to_executor=-376ms（claim 早于 status=2 写入 staging 的 race） |
| `bench_hk_02208_20260827_030418_001` | exit | 4758 | 缺少 executor_claim_ms |
| `bench_hk_03308_20260827_030418_002` | entry | 4759 | ms_ready_to_executor=-157ms（claim 早于 status=2 写入 staging 的 race） |
| `bench_hk_02208_20260827_030418_005` | entry | 4762 | ms_ready_to_executor=-3ms（claim 早于 status=2 写入 staging 的 race） |
| `bench_hk_03308_20260827_030418_006` | entry | 4763 | ms_ready_to_executor=-214ms（claim 早于 status=2 写入 staging 的 race） |

## Test 1 — 执行延时 · 开仓（entry）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 6 | 6726.5ms | 7100.5ms | 3737569.1 | 1933.3ms | 3007.0ms | 8266.0ms |
| 入库 → 本 leg 完成 | 6 | 8174.7ms | 8680.0ms | 2967881.5 | 1722.8ms | 4770.0ms | 9591.0ms |
| SDK 报单 API | 6 | 47.0ms | 47.5ms | 4.4 | 2.1ms | 43.0ms | 49.0ms |
| SDK 调用（us→ms） | 6 | 46.1ms | 46.8ms | 4.9 | 2.2ms | 41.8ms | 47.6ms |
| 触发 → 报单发出 | 6 | 47.3ms | 48.0ms | 5.1 | 2.3ms | 43.0ms | 49.0ms |
| 报单 → 成交 | 6 | 4199.5ms | 4666.5ms | 3236084.3 | 1798.9ms | 730.0ms | 5859.0ms |
| 触发 → 成交 | 6 | 4246.8ms | 4715.0ms | 3236764.6 | 1799.1ms | 777.0ms | 5908.0ms |

## Test 1 — 执行延时 · 平仓（exit）

| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |
|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|
| 策略 shell 总耗时 | 6 | 6532.5ms | 7290.5ms | 4169666.3 | 2042.0ms | 2445.0ms | 7919.0ms |
| requeue → 本 leg 完成 | 6 | 8209.8ms | 9094.0ms | 4911225.4 | 2216.1ms | 3758.0ms | 9534.0ms |
| SDK 报单 API | 6 | 42.8ms | 43.0ms | 15.0 | 3.9ms | 38.0ms | 47.0ms |
| SDK 调用（us→ms） | 6 | 41.9ms | 41.7ms | 13.9 | 3.7ms | 37.5ms | 46.1ms |
| 触发 → 报单发出 | 6 | 43.0ms | 43.0ms | 13.6 | 3.7ms | 38.0ms | 47.0ms |
| 报单 → 成交 | 6 | 4389.5ms | 5003.0ms | 2915001.9 | 1707.3ms | 1000.0ms | 5649.0ms |
| 触发 → 成交 | 6 | 4432.5ms | 5044.0ms | 2910230.7 | 1705.9ms | 1047.0ms | 5696.0ms |

## 逐条样本

- `bench_hk_02208_20260827_030418_001` leg=entry f_id=4758 sched_total=1116ms shell=8266ms api=49ms
- `bench_hk_02208_20260827_030418_001` leg=exit f_id=4758 sched_total=807ms shell=7309ms api=38ms
- `bench_hk_03308_20260827_030418_002` leg=entry f_id=4759 sched_total=1471ms shell=7425ms api=43ms
- `bench_hk_03308_20260827_030418_002` leg=exit f_id=4759 sched_total=1141ms shell=6697ms api=39ms
- `bench_hk_02208_20260827_030418_003` leg=entry f_id=4760 sched_total=824ms shell=8138ms api=48ms
- `bench_hk_02208_20260827_030418_003` leg=exit f_id=4760 sched_total=1507ms shell=7272ms api=44ms
- `bench_hk_03308_20260827_030418_004` leg=entry f_id=4761 sched_total=1183ms shell=6747ms api=48ms
- `bench_hk_03308_20260827_030418_004` leg=exit f_id=4761 sched_total=856ms shell=7919ms api=47ms
- `bench_hk_02208_20260827_030418_005` leg=entry f_id=4762 sched_total=1535ms shell=6776ms api=47ms
- `bench_hk_02208_20260827_030418_005` leg=exit f_id=4762 sched_total=1260ms shell=2445ms api=47ms
- `bench_hk_03308_20260827_030418_006` leg=entry f_id=4763 sched_total=1970ms shell=3007ms api=47ms
- `bench_hk_03308_20260827_030418_006` leg=exit f_id=4763 sched_total=1646ms shell=7553ms api=42ms

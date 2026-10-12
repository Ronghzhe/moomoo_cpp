# Test 5 — API 包装延时对比 (20260901_080751)

**时间：** 2026-09-01 08:10:31 (UTC+8)
**环境：** OpenD SIMULATE @ 127.0.0.1:11121
**CSV：** `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/api_wrapper_latency.csv`

包装层 = `OrderClient` / `AccountClient`；裸调用 = `OpenSecTradeContext` SDK 直调。

| api | path | n | p50_ms | p99_ms | mean_ms | stdev_ms |
|-----|------|--:|-------:|-------:|--------:|---------:|
| cancel | raw | 10 | 27.692 | 56.232 | 31.025 | 10.12 |
| cancel | wrapped | 10 | 26.584 | 46.298 | 29.104 | 7.409 |
| cancel | **overhead (wrap−raw)** | 10 | -0.833 | 21.938 | -1.921 | 14.071 |
| query | raw | 10 | 7.369 | 9.878 | 7.787 | 1.222 |
| query | wrapped | 10 | 23.279 | 32.73 | 24.728 | 4.054 |
| query | **overhead (wrap−raw)** | 10 | 15.145 | 24.933 | 16.941 | 3.866 |

## 通过标准（草案）

- P0: 每个 API（place/query/cancel）有 raw / wrapped 两套数据
- P1: 包装开销 p50 < 1ms（place；query/cancel 同口径观测）

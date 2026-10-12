# Test 5 — API 包装延时对比 (20260831_082629)

**时间：** 2026-08-31 08:26:33 (UTC+8)
**环境：** OpenD SIMULATE @ 127.0.0.1:11121
**CSV：** `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/api_wrapper_latency.csv`

包装层 = `OrderClient` / `AccountClient`；裸调用 = `OpenSecTradeContext` SDK 直调。

| api | path | n | p50_ms | p99_ms | mean_ms | stdev_ms |
|-----|------|--:|-------:|-------:|--------:|---------:|
| place | raw | 8 | 136.78 | 145.763 | 135.604 | 7.744 |
| place | wrapped | 7 | 133.716 | 146.739 | 135.807 | 6.615 |
| place | **overhead (wrap−raw)** | 7 | -1.61 | 17.091 | 0.782 | 9.233 |
| query | raw | 0 | 0.0 | 0.0 | 0.0 | 0.0 |
| query | wrapped | 0 | 0.0 | 0.0 | 0.0 | 0.0 |
| query | **overhead (wrap−raw)** | 0 | 0.0 | 0.0 | 0.0 | 0.0 |

## 通过标准（草案）

- P0: 每个 API 有 raw / wrapped 两套数据
- P1: place 包装开销 p50 < 1ms

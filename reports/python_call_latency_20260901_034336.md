# Test 4 — Python 调用延时 (20260901_034336)

**时间：** 2026-09-01 03:43:43 (UTC+8)
**CSV：** `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/python_call_latency.csv`

## 统计

| module | operation | n | p50_ms | p99_ms | mean_ms | stdev_ms | min_ms | max_ms | ok |
|--------|-----------|--:|-------:|-------:|--------:|---------:|-------:|-------:|----|
| telegram | send_photo | 3 | 698.9337 | 1021.861 | 801.5238 | 160.7074 | 677.1864 | 1028.4514 | 3/3 |
| telegram | send_text | 5 | 659.0839 | 686.7846 | 662.7353 | 12.935 | 651.6794 | 687.8973 | 5/5 |

## 通过标准（草案）

- P1: TG send_text p50 < 2000ms
- P1b: TG send_photo p50 < 3000ms（网络正常）
- P2: orderbook_eval p50 < 10ms

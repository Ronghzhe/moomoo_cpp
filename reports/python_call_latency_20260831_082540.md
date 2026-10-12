# Test 4 — Python 调用延时 (20260831_082540)

**时间：** 2026-08-31 08:25:40 (UTC+8)
**CSV：** `/home/ubuntu/workspace/moomoo_CPP/strategy/task_system/bench_logs/python_call_latency.csv`

## 统计

| module | operation | n | p50_ms | p99_ms | mean_ms | stdev_ms | min_ms | max_ms | ok |
|--------|-----------|--:|-------:|-------:|--------:|---------:|-------:|-------:|----|
| condition_engine | orderbook_eval | 5000 | 0.0014 | 0.0056 | 0.0017 | 0.0014 | 0.0013 | 0.06 | 5000/5000 |
| strategy_main | volume_spike_eval | 5000 | 0.001 | 0.0036 | 0.0011 | 0.0008 | 0.0008 | 0.0239 | 5000/5000 |
| telegram | send_text | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0/1 |

## 通过标准（草案）

- P1: TG send_text p50 < 2000ms
- P2: orderbook_eval p50 < 10ms

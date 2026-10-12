# Test 3 — 行情延时 (20260831_082540)

**时间：** 2026-08-31 08:26:43 (UTC+8)
**环境：** OpenD 127.0.0.1:11121，BasicQot 订阅（非 OrderBook）
**原始数据：** `reports/market_data_latency_20260831_082540/raw/`

## 首条 push 延时（5 轮/标的/语言）

| lang | symbol | n | connect p50 ms | subscribe p50 ms | first_push p50 ms | first_push mean ms | min | max |
|------|--------|--:|---------------:|-----------------:|------------------:|-------------------:|----:|----:|
| cpp | HK.02208 | 5 | 1.361 | 1.176 | 1.143 | 51.881 | 0.774 | 254.242 |
| cpp | US.AAPL | 5 | 1.434 | 7.854 | 7.64 | 58.087 | 6.811 | 260.104 |
| python | HK.02208 | 5 | 3.382 | 5.434 | 5.054 | 5.063 | 4.611 | 5.482 |
| python | US.AAPL | 5 | 4.082 | 11.871 | 13.556 | 13.354 | 11.749 | 14.656 |

## C++ vs Python（first_push p50）

- **HK.02208**: C++ p50=1.143ms, Python p50=5.054ms → C++ 更快（Python/C++ 比=4.42）
- **US.AAPL**: C++ p50=7.64ms, Python p50=13.556ms → C++ 更快（Python/C++ 比=1.77）

## 说明

- 本次仅测 **subscribe → first BasicQot push**；push 间隔（p99）需扩展 bench 逐条打点，未纳入本轮。
- OrderBook 盘口订阅与策略路径上的 `first_book_push_ms` 不同，见 Test 1 staging 字段。
- 失败样本：0

## 通过标准

- P0: 全部 20 样本 ok ✅
- P1: 首 push 在 15s 超时内到达 ✅

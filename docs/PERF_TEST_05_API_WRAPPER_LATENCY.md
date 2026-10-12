# Test 5 — API 包装延时对比

> **范围：** 同一 OpenD 能力，**裸 SDK/API 调用** 与 **程序封装层**（OrderClient、C++ TradeService 等）返回耗时 **对比**。

**状态：** ✅ place（`20260831_082629`）+ query/cancel（`20260901_080751`）已完成

---

## 1. 要测什么

| 能力 | 裸调用 | 封装调用 |
|------|--------|----------|
| 下单 PlaceOrder | SDK 直调 | `order_client` / C++ place 路径 |
| 查单 | SDK 直调 `order_list_query` | `OrderClient.get_order` |
| 撤单 | SDK 直调 `modify_order(CANCEL)` | `OrderClient.cancel_order` |
| 行情订阅 | SDK 直调 | OpenD 连接层封装 |

输出：**包装开销 = 封装耗时 − 裸 API 耗时**（同环境、同参数、N 次统计）。

---

## 2. 怎么跑

```bash
cd /home/ubuntu/workspace/moomoo_CPP

# 全量（place + query/cancel）；默认 gap≥2.2s，避开 15 次/30s 频控
/home/ubuntu/workspace/moomoo_python/.venv/bin/python bench/api_wrapper_latency.py

# 只补 query/cancel（不先跑 place，避免烧额度）
/home/ubuntu/workspace/moomoo_python/.venv/bin/python bench/api_wrapper_latency.py \
  --skip-place --query-cancel-iters 10 --gap-sec 2.2
```

配置：`config/bench.yaml`（`order_gap_sec: 2.2`）  
CSV：`strategy/task_system/bench_logs/api_wrapper_latency.csv`  
报告：`reports/api_wrapper_latency_<batch>.md`

---

## 3. 与 Test 1 的关系

Test 1 的 `entry_place_sdk_us` 是**执行路径上** SDK 段；Test 5 在**受控 micro-bench** 中分离「SDK 本身」与「前后包装」，用于评估是否值得再薄封装。

---

## 4. 通过标准（草案）

| 级别 | 条件 | 结果 |
|------|------|------|
| P0 | 每个 API 有 raw / wrapped 两套数据 | ✅ place/query/cancel |
| P1 | 包装开销 p50 < 1ms（place） | ✅ place −1.6 ms；cancel −0.8 ms；query 15.1 ms（见综合报告） |
| P2 | C++ 包装开销 ≤ Python 包装开销（同 API） | 未测 |

---

## 5. 最新结果摘要

| api | wrap−raw p50 | n | 批次 |
|-----|-------------:|--:|------|
| place | −1.6 ms | 7 | `20260831_082629` |
| query | 15.1 ms | 10 | `20260901_080751` |
| cancel | −0.8 ms | 10 | `20260901_080751` |

综合报告：`reports/C++框架性能测试综合报告_20260901.md` §8

---

## 6. 相关

- [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md)
- `reports/OpenD_CPP_Python_综合对比.csv`

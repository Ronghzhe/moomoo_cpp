# Test 3 — 行情延时

> **范围：** 每条行情 **本地收到时刻** vs **数据时间** vs **更新时间** 的差值（均值/中位）。  
> 不再以「订阅→首推」为主口径。

**状态：** ✅ 新口径已测（batch `20260903_000444`）

**最新报告：** [行情收到延时_20260903_000444.md](../reports/market_data_recv_latency/行情收到延时_20260903_000444.md)  
**综合报告第 6 节：** [C++框架性能测试综合报告.md](../reports/C++框架性能测试综合报告.md)

---

## 1. 要测什么

| 时刻 | 来源 |
|------|------|
| 本地收到 | 本机 `on_recv_rsp` 时刻（北京时间） |
| 数据时间 | BasicQot `data_date` + `data_time`（API 来自 `pb.updateTime`） |
| 推送更新时间 | 与数据时间同源（BasicQot 无第二套戳） |
| 快照更新时间 | `get_market_snapshot.update_time` |

指标：`本地−数据`、`本地−推送更新`、`本地−快照更新`、`快照更新−数据`。

---

## 2. 如何跑

```bash
cd /home/ubuntu/workspace/moomoo_CPP
PYTHONPATH=/home/ubuntu/workspace/MMAPI4Python_10.9.6908 \
  /home/ubuntu/workspace/venv/bin/python bench/market_data_recv_latency.py \
  --duration-sec 90 --min-pushes 150 \
  --out-dir reports/market_data_recv_latency
```

---

## 3. 通过标准（草案）

| 级别 | 条件 |
|------|------|
| P0 | 能收到 push，并解析出数据时间 |
| P1 | 盘中活跃样本给出「本地−数据」中位 |
| P2 | 港/美分开；休市样本不纳入主结论 |

---

## 4. 相关

- [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md)
- 旧首推报告仅作历史：`reports/market_data_latency_20260831_*`

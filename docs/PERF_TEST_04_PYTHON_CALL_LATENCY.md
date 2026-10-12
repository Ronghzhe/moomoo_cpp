# Test 4 — Python 调用延时

> **范围：** 策略/任务系统中 **Python 侧** 可调用的耗时模块：公式计算、Telegram 通知等（**不含** C++ strategy_cpp 主路径）。

**状态：** ✅ 公式首轮完成（`20260831_082540`）；✅ TG 补测完成（`20260901_034336`，paper 群 send_text×5 + send_photo×3）

---

## 1. 要测什么

| 模块 | 典型调用 |
|------|----------|
| 公式 / live_eval | 盘口条件、自定义表达式求值 |
| Telegram | 发消息、发图片（`telegram_client`） |
| 其他 | 按需补充（钉钉、持仓同步等） |

目标：单次调用 p50/p99；批量/并发下的排队情况。

---

## 2. 计划实现

| 项 | 方向 |
|----|------|
| 脚本 | `bench/python_call_latency.py` 或 pytest benchmark |
| 输出 | CSV：`module`, `operation`, `latency_ms`, `ok` |
| 隔离 | 不连 OpenD；TG 可走 mock 或专用 test chat |

---

## 3. 与 Test 1 的关系

C++ 主链（`strategy_cpp`）已迁移后，**热路径不应依赖 Python**；本测试用于：

- 仍走 Python 的 Worker / Web / 辅助脚本
- 对比迁移前后是否还有 Python 瓶颈

---

## 4. 通过标准（草案）

| 级别 | 条件 |
|------|------|
| P0 | 各模块有 baseline 数字 |
| P1 | TG 单次发送 p50 < 2s（网络正常） |
| P2 | 公式单次 eval p50 < 10ms（典型表达式） |

---

## 5. 相关

- [PERF_TEST_INDEX.md](./PERF_TEST_INDEX.md)
- `moomoo_python/strategy/` 内 telegram、eval 相关模块

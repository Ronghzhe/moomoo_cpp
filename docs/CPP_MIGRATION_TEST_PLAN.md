# OpenD Python → C++ 迁移测试计划

> Phase 0 交付物。目标：在**不影响实盘 Python 程序**的前提下，建立可重复的 C++ 对齐验证体系。

## 1. 原则

| 原则 | 说明 |
|------|------|
| **并行轨道** | 所有 C++ 代码与测试在 `moomoo_CPP/`；不修改 `moomoo_python/strategy/` 运行逻辑 |
| **默认 SIMULATE** | L1 及以下只用模拟盘；REAL 仅在 L5 且显式 `--allow-real` |
| **只读 golden** | 从 Python 日志/config **复制** fixture，不回写生产目录 |
| **行为优先** | 通过标准以订单参数、信号、状态机为准；延迟仅作参考 |

## 2. 测试分层（L0～L5）

```
L0 单元测试        ── 无 OpenD，离线 CI
L1 OpenD 契约测试  ── SIMULATE，独立 bench
L2 Golden parity   ── 回放 fixture，diff 输出
L3 集成测试        ── C++ 策略进程 paper 端到端
L4 Shadow 模式     ── 旁路对比，不下单
L5 REAL 验收       ── 小仓位人工窗口，最后执行
```

---

## 3. L0 — 单元测试（零 OpenD）

**环境：** 任意机器，无需 OpenD。  
**工具：** GoogleTest / Catch2 + `ctest`。

| ID | 模块 | 用例 | 输入 | 通过标准 |
|----|------|------|------|----------|
| L0-01 | Config | 解析 US fixture JSON | `tests/fixtures/strategy_config_us_fixture.json` | 字段与 Python `StrategyConfig` 等价 |
| L0-02 | Config | 解析 HK fixture JSON | `tests/fixtures/strategy_config_hk_fixture.json` | 同上 |
| L0-03 | Config | 解析 HK+US 混合 fixture | `strategy_config_hk_us_fixture.json` | 多 market symbol 不混淆 |
| L0-04 | HK tick | 价格 tick 对齐 | 表驱动用例 | 与 `test_hk_tick_utils.py` 一致 |
| L0-05 | Condition | orderbook 条件 | ask_1/bid_1 fixture | 触发/不触发与 Python evaluator 一致 |
| L0-06 | Condition | kline_close / volume | kline fixture | 同上 |
| L0-07 | net_split | 多腿分配 | `test_entry_net_planner.py` 场景 | leg qty 一致 |
| L0-08 | net_split | rollback | `test_net_split_rollback.py` | 回滚后状态一致 |
| L0-09 | Rate limit | 退避序列 | `test_order_rate_limit_retry.py` | 等待时间序列一致 |
| L0-10 | Order parse | side/type/tif 字符串 | NORMAL/MARKET/BUY/SELL/DAY/GTC | 与 `test_order_client_coerce.py` 一致 |

**Python 测试映射：** 见 §7。

---

## 4. L1 — OpenD 契约测试（SIMULATE）

**环境：** OpenD `127.0.0.1:11121`，`TrdEnv_Simulate`。  
**脚本：** 扩展现有 `moomoo_CPP/cpp/bench_*.cpp` + `scripts/run_all.sh`。

| ID | 用例 | Python 参照 | C++ 目标 | 通过标准 |
|----|------|------------|----------|----------|
| L1-01 | K 线拉取 | `bench_kline.py` | `bench_kline` | 条数一致；p50 在已知倍率内（≈2.4×） |
| L1-02 | 行情推送 | `bench_push.py` | `bench_push` | 收到首条 push |
| L1-03 | 下单+推送 | `bench_order.py` | `bench_order` | place/push 均成功 |
| L1-04 | 查单 | `bench_query_cancel.py` | `bench_query_cancel` | 返回 order_id 匹配 |
| L1-05 | 撤单 | 同上 | 同上 | 状态变为终态 |
| L1-06 | OrderClient.place | `order_client.py` | 未来 `TradeService::Place` | ret=0，order_id 非空 |
| L1-07 | OrderClient.cancel | 同上 | `TradeService::Cancel` | 撤单成功 |
| L1-08 | Push hub | `test_order_push_hub.py` | `OrderPushCache` | version 递增，wait_update 唤醒 |
| L1-09 | 限频重试 | `bench_real_roundtrip` 逻辑 | `RateLimiter` | 遇限频不崩溃，重试成功 |

**隔离：** 不读写 Python task queue；jsonl 输出到 `reports/contract/`。

---

## 5. L2 — Golden Parity（行为对齐）

**方法：**

1. **录制（只读）：** 从 Python paper 跑或 `daily_logs/` 提取 `(config, orderbook/kline 序列) → (signals, orders, final_state)`。
2. **回放：** C++ 读同一输入，输出 diff。
3. **首批场景（5～10 个）：**

| ID | 场景 | 来源测试/日志 | 比对字段 |
|----|------|--------------|----------|
| L2-01 | US 市价入场 + PnL 出场 | US fixture + paper log | side, qty, order_type, status |
| L2-02 | HK sim_market 入场 | HK fixture | price offset, tick 对齐 |
| L2-03 | HK short entry band | `test_hk_short_entry_band.py` | 价格区间、确认逻辑 |
| L2-04 | net_split 多腿 | `test_entry_net_split_hk_us.py` | 各 leg qty、顺序 |
| L2-05 | net_split rollback | `test_net_split_rollback.py` | 回滚后持仓 |
| L2-06 | book_guard 紧急平仓 | `test_book_guard_emergency_exit.py` | 触发条件、平仓单 |
| L2-07 | staged_take_profit | `test_staged_take_profit_alloc.py` | 分批比例 |
| L2-08 | push_hybrid fill | `test_order_push_hub.py` + order log | fill 检测时序（±1 tick） |

**通过标准：**

- 订单 `side/qty/price/type` **100% 一致**
- 状态机终态一致
- 时间戳允许 **±1 tick** 或配置定义的 poll 窗口

**目录：**

```
tests/fixtures/           # config（已有）
tests/golden/             # 期望输出 JSON
tests/replay/             # orderbook/kline 序列
```

---

## 6. L3 / L4 / L5

### L3 — 集成（C++ 策略进程，paper）

| ID | 场景 | 命令（规划） | 通过标准 |
|----|------|-------------|----------|
| L3-01 | 单 config 端到端 | `./strategy_cpp -c fixtures/... --mode paper` | 完整开平仓，日志写入 `daily_logs_cpp/` |
| L3-02 | 多 symbol | 2～3 symbol 同 config | 无交叉污染 |
| L3-03 | 断连恢复 | 模拟 OpenD 断连 | 重连后不重复下单 |
| L3-04 | 限频 | 连续下单 | 退避后成功 |

**隔离：** 不写 Python CSV/MySQL；独立日志目录。

### L4 — Shadow（旁路，零下单风险）

```
OpenD quote ──→ Python strategy（真实/ paper 不变）
             └──→ C++ strategy（dry_run=true，只算信号）
                      └── diff 日志
```

| 通过标准 | dry-run 的 would-place 与 Python did-place 一致率 ≥ 99%（非白名单 symbol 除外） |

### L5 — REAL 验收（最后，需审批）

| 项 | 要求 |
|----|------|
| 前置 | L0～L4 全绿 |
| 标的 | 流动性好、小仓位（如 HK.02208 × 200） |
| 次数 | Python/C++ 各 **10 次** round-trip |
| 窗口 | 非高峰、人工在场 |
| 回滚 | 随时停 C++，Python 不受影响 |

---

## 7. Python 现有测试 → C++ 映射

| Python 测试文件 | C++ 目标层 | 优先级 |
|----------------|-----------|--------|
| `test_order_client_coerce.py` | L0-10, L1-06 | P0 |
| `test_order_push_hub.py` | L1-08, L2-08 | P0 |
| `test_order_rate_limit_retry.py` | L0-09, L1-09 | P0 |
| `test_hk_tick_utils.py` | L0-04 | P0 |
| `test_hk_short_entry_band.py` | L2-03 | P0 |
| `test_hk_short_entry_confirm.py` | L2-03 | P0 |
| `test_submit_price_hk_short.py` | L2-03 | P0 |
| `test_entry_net_planner.py` | L0-07 | P0 |
| `test_entry_net_split_*.py` | L0-07, L2-04 | P0 |
| `test_net_split_rollback.py` | L0-08, L2-05 | P0 |
| `test_exit_net_planner.py` | L0-07 | P0 |
| `test_book_guard_emergency_exit.py` | L2-06 | P0 |
| `test_staged_take_profit_alloc.py` | L2-07 | P0 |
| `test_gateway_broker_client.py` | Phase 4（可选保留 Python gateway） | P1 |
| `test_opend_gateway_components.py` | 同上 | P1 |
| `test_trade_executor_*.py` | L3 或保留 Python 编排 | P1 |
| `test_position_sync_*.py` | Phase 4 监控层 | P2 |
| `test_perf_*.py` | 非迁移范围 | — |

---

## 8. 目录与脚本（规划）

```
moomoo_CPP/
├── docs/                          # 本文档 + 模块映射 + API 对照
├── tests/
│   ├── fixtures/                  # 脱敏 config（Phase 0 已建）
│   ├── golden/                    # L2 期望输出
│   ├── replay/                    # L2 行情序列
│   ├── unit/                      # L0
│   └── contract/                  # L1 结果 jsonl
├── scripts/
│   ├── run_tests_l0.sh            # 第二步实现
│   ├── run_tests_l1.sh
│   └── run_parity_l2.sh
└── src/                           # 第二步起：共享库
```

---

## 9. 里程碑与门禁

| 里程碑 | 内容 | 门禁 |
|--------|------|------|
| M0 | Phase 0 文档 + fixtures | 本文档 + 3 份 docs + fixtures |
| M1a | `moomoo_core` + L0 单测 | `./scripts/run_tests_l0.sh` **17/17** 通过 |
| M1b | `moomoo_trade` + L1 契约 | `./scripts/run_tests_l1.sh` place/push/query/cancel |
| M1 | `libmoomoo_trade` + L0/L1 全绿 | L0 + L1 通过 |
| M2 | Config + ConditionEngine + L2 | 首批 5 个 golden 全绿 |
| M3 | strategy_cpp paper + L3/L4 | shadow 一致率 ≥ 99% |
| M4 | REAL L5 + 灰度切换 | 10 次 round-trip 行为对齐 |

---

## 10. 实盘隔离检查清单

每次跑测试前确认：

- [ ] 未修改 `moomoo_python/strategy/task_system/` 运行脚本
- [ ] 未向 MySQL `strategy_tasks` 写入 C++ 测试任务
- [ ] REAL 测试使用独立脚本 + `--allow-real`
- [ ] C++ 日志目录与 Python `daily_logs/` 分离
- [ ] OpenD 测试账户为 SIMULATE（L5 除外）

---

*版本：Phase 0 v1.0 | 日期：2026-08-04*

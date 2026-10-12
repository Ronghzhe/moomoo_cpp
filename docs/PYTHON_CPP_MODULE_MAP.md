# Python → C++ 模块映射表

> Phase 0 交付物。用于规划 C++ 重写范围与优先级。

## 1. 规模概览

| 指标 | Python (`moomoo_python`) | C++ (`moomoo_CPP` 现状) |
|------|--------------------------|-------------------------|
| 生产代码 | ~211 `.py`，~60k LOC | ~6 bench `.cpp`，~1.6k LOC |
| 策略核心 | `strategy_main.py` ~11k LOC | 无 |
| 单元测试 | `strategy/tests/` 38 文件 | bench 对比脚本 only |

---

## 2. 映射表（按迁移阶段）

### Phase 1 — OpenD 基础层（P0）

| Python 模块 | LOC(约) | C++ 目标模块 | 复用来源 | 优先级 |
|-------------|---------|-------------|----------|--------|
| `account_client.py` | 290 | `src/opend/AccountClient.hpp` | bench + SDK FTAPIDemoBase | P0 |
| `order_client.py` | 310 | `src/trade/OrderClient.hpp` | `bench_order.cpp`, `bench_real_roundtrip.cpp` | P0 |
| `order_push_hub.py` | 120 | `src/trade/OrderPushCache.hpp` | `bench_order.cpp` OnPush | P0 |
| `kline_client.py` | 650 | `src/quote/KlineService.hpp` | `bench_kline.cpp` | P0 |
| `strategy/broker/interfaces.py` | 60 | `src/trade/IOrderBroker.hpp` | 新设计 | P0 |
| `strategy/broker/direct_broker.py` | 85 | `src/trade/DirectBroker.hpp` | OrderClient 包装 | P0 |
| `strategy/broker/gateway_broker.py` | 110 | `src/trade/GatewayBroker.hpp` | 可选；或 Phase 4 保留 Python gateway | P1 |
| `strategy/broker/fallback_broker.py` | 70 | `src/trade/FallbackBroker.hpp` | 同上 | P1 |
| `strategy/broker/factory.py` | 50 | `src/trade/BrokerFactory.hpp` | 同上 | P1 |
| `strategy/opend_gateway/server.py` | 530 | `apps/opend_gateway/` 或保留 Python | Python gateway 可延后 | P1 |
| `strategy/opend_gateway/rate_limiter.py` | — | `src/common/RateLimiter.hpp` | `bench_real_roundtrip.cpp` | P0 |
| `order_errors.py` | — | `src/trade/OrderErrors.hpp` | 错误码分类 | P0 |
| `trade_unlock.py` / `trade_unlock_gate.py` | — | `src/trade/UnlockGate.hpp` | REAL 解锁 | P0 |

### Phase 2 — 配置 + 条件引擎（P0）

| Python 模块 | LOC(约) | C++ 目标模块 | 优先级 |
|-------------|---------|-------------|--------|
| `strategy_loader.py` | 1700 | `src/config/StrategyLoader.hpp` | P0 |
| `strategy_types.py` | 400 | `src/config/StrategyTypes.hpp` | P0 |
| `condition_engine/` (~1400) | | `src/engine/ConditionEngine.hpp` | P0 |
| `condition_engine/evaluators/orderbook` | | `src/engine/eval/OrderbookEval.hpp` | P0 |
| `condition_engine/evaluators/kline_*` | | `src/engine/eval/KlineEval.hpp` | P0 |
| `condition_engine/evaluators/volume` | | `src/engine/eval/VolumeEval.hpp` | P0 |
| `condition_engine/evaluators/pnl` | | `src/engine/eval/PnlEval.hpp` | P0 |
| `condition_engine/evaluators/trailing_*` | | `src/engine/eval/TrailingEval.hpp` | P0 |
| `condition_engine/evaluators/staged_take_profit` | | `src/engine/eval/StagedTpEval.hpp` | P0 |
| `hk_tick_utils.py` | | `src/market/HkTickUtils.hpp` | P0 |
| `hk_short_params.py` | | `src/market/HkShortParams.hpp` | P0 |

### Phase 3 — 策略引擎核心（P0）

| Python 模块 | LOC(约) | C++ 目标模块 | 说明 |
|-------------|---------|-------------|------|
| `strategy_main.py` | **11000** | 拆分为多个 C++ 模块 | 最大单体，需分解 |
| ↳ 状态机 / symbol loop | | `src/strategy/SymbolStateMachine.hpp` | |
| ↳ 下单重试 / SIM_MARKET | | `src/strategy/OrderExecutor.hpp` | |
| ↳ fill 检测 push_hybrid | | `src/strategy/FillDetector.hpp` | |
| ↳ orderbook 订阅分发 | | `src/strategy/OrderbookDispatcher.hpp` | |
| `entry_net_split.py` | 1000 | `src/strategy/EntryNetSplit.hpp` | P0 |
| `exit_net_split.py` | 400 | `src/strategy/ExitNetSplit.hpp` | P0 |
| `book_guard_emergency_exit.py` | 1160 | `src/strategy/BookGuard.hpp` | P0 安全 |
| `force_flatten.py` | 470 | `apps/force_flatten` | P1 |
| `strategy_store.py` | 280 | `src/log/StrategyStore.hpp` | CSV 账本 |
| `strategy_logger.py` | 1500 | `src/log/StrategyLogger.hpp` | P1 |
| `strategy_notify.py` / `strategy_dingding.py` | | 可 Phase 4 保留 Python HTTP 调用 | P2 |

### Phase 4 — 任务系统 + 运维（P1/P2）

| Python 模块 | LOC(约) | C++ 目标 | 建议 |
|-------------|---------|---------|------|
| `task_system/task_worker_main.py` | 530 | `apps/task_worker` 或保留 Python | 可先保留 Python 编排 |
| `task_system/trade_executor.py` | 990 | `apps/trade_executor` 或保留 Python | 同上 |
| `task_system/task_mapper.py` | 500 | `src/task/TaskMapper.hpp` | P1 |
| `task_system/task_result.py` | 450 | `src/task/TaskResult.hpp` | P1 |
| `task_system/position_sync_monitor.py` | 1450 | 保留 Python 或 Phase 4 | P2 |
| `task_system/db.py` | 30 | `src/db/MysqlClient.hpp` | P1 |
| `dingding/` + `telegram/` | ~2500 | 保留 Python | P2 |

### 不迁移 / 低优先级

| 模块 | 原因 |
|------|------|
| `trade_web/` | 独立 Web 栈 |
| `research_reports/` | 研究脚本 |
| `tools/` 运维脚本 | 可继续 Python |
| `dingding/commands.py` | 交互 bot，非交易热路径 |

---

## 3. C++ 已有可复用资产

| 文件 | 可提取为 |
|------|----------|
| `cpp/bench_kline.cpp` | `QuoteService::FetchKline` |
| `cpp/bench_push.cpp` | `QuoteService::SubscribeBasicQot` |
| `cpp/bench_order.cpp` | `TradeService::Place/Cancel/Push` |
| `cpp/bench_query_cancel.cpp` | `TradeService::Query/Cancel` |
| `cpp/bench_real_roundtrip.cpp` | REAL 下单 + 限频重试 + fill poll |
| `cpp/CMakeLists.txt` | SDK 链接、ABI、protobuf 配置 |
| SDK `FTAPIDemoBase` | 连接层基类（后续可改 async） |

---

## 4. 建议 C++ 目录结构

```
moomoo_CPP/
├── src/
│   ├── opend/          # 连接、账户
│   ├── quote/          # K线、推送
│   ├── trade/          # 下单、push hub、broker
│   ├── config/         # JSON 配置
│   ├── engine/         # 条件引擎
│   ├── strategy/       # 状态机、执行
│   ├── task/           # 任务映射（可选）
│   ├── log/            # CSV/日志
│   └── common/         # 限频、工具
├── apps/
│   ├── strategy_cpp    # 对标 strategy_main.py
│   ├── order_cli       # 对标 order_main.py
│   └── force_flatten
├── tests/
├── cpp/                # 保留 bench（或迁到 apps/bench/）
└── docs/
```

---

## 5. 粗算 C++ 工作量

| 阶段 | 预估 LOC | 说明 |
|------|----------|------|
| Phase 1 | 3k–5k | OpenD 层 + broker |
| Phase 2 | 5k–8k | config + condition engine |
| Phase 3 | 15k–25k | strategy_main 分解 |
| Phase 4 | 5k–10k | task + db（若全 C++） |
| **合计** | **25k–40k** | 不含 dingding bot |

---

*版本：Phase 0 v1.0 | 日期：2026-08-04*

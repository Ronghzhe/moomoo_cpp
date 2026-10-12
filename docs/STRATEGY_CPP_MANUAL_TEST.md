# strategy_cpp 手动测试

> 启动用 `./run_strategy.sh`；`mode: paper|real` 在 JSON 里配置。

## 启动

```bash
cd /home/ubuntu/workspace/moomoo_CPP

# 干跑 judge（不下单，需显式 --dry-run）
./run_strategy.sh -c strategy/config/strategy_config_us.json \
  --max-ticks 20 --poll-sec 5 --dry-run

# 或直接用预设
./run_strategy.sh -hk --max-ticks 50
./run_strategy.sh -us --max-ticks 50
```

## Phase 3 能力（当前）

- 状态机：status 0→1/2→3；entry_qty 续开（status=1）；budget/lot_size  sizing
- 成交检测：push + poll；12s stale cancel+reprice；CANCELLED_ALL 重试
- 下单定价：SIM_MARKET 逐级偏移 + HK tick 对齐；book_guard 跳过倒挂盘口
- REAL unlock：MOOMOO_TRADE_PWD 或 Fernet 密文（复用 Python secrets）
- acc_id 路由：JSON account.acc_id
- K 线条件 + exit kline_close / trailing_take_profit
- CSV / Telegram / JSON runtime 写回

## 尚未实现

- 钉钉通知、force_flatten
- task_system / MySQL trade DB / gateway broker

## net_split 多腿（可选）

代码已接入 `order_pending_loop`，需与 Python 一致开启环境变量：

```bash
export ENTRY_NET_SPLIT_ENABLED=1
export CPP_NET_SPLIT_ENABLED=1   # run_strategy.sh 在检测到 ENTRY 时会自动 export
./run_strategy.sh -c path/to/config.json
```

默认关闭；paper 验证多腿/rollback 后再开。

## Phase 3+ 已加入

- book_guard 应急平仓：倒挂且无有效缓存 → last_price 限价 → 12s 未成交改市价
- 港股开空专用：卖一向下 1% 价差保护；3s 未成交撤单改价（最多 5 次）
- symbol_tradability：snapshot 轮询不可交易品种；报单被拒含 not tradable/停牌 等则等待重试

## 回归

```bash
cd build && ctest --output-on-failure
./scripts/run_tests_l1.sh
```

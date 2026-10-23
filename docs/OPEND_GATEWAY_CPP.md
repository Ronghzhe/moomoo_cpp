# C++ OpenD Place Gateway

独立于 Python `opend_gateway`（`:18765`）。C++ strategy 只连本进程（默认 `:18766`）。

## 职责

- 统一排队（优先级 + 同优先进 FIFO）
- 按 `acc_id` 限频（默认 14/30s，最小间隔 0.02s，预留 1）
- `request_id` 幂等缓存（300s）+ 在途合并
- 请求等待超时（默认 15s）
- 单进程 **一条** OpenD 交易连接（Moomoo SDK 仅支持一次 `InitTrd`）；按 `trd_env`（SIMULATE/REAL）切换报单环境，可同时接模拟盘与实盘任务
- dry-run / live
- `/health` `/metrics` + `gateway_events.jsonl`

**不做：** 成交 push 转发（strategy 仍自连 OpenD 收 push）。

## 启动

```bash
# dry-run（默认）
./build/apps/opend_gateway_cpp --bind-port 18766 --log-dir ./sys_log/gateway_cpp

# live
./build/apps/opend_gateway_cpp --live --bind-port 18766 --log-dir ./sys_log/gateway_cpp

# 或
GATEWAY_CPP_MODE=--live ./scripts/gateway_cpp.sh start
```

## Strategy / Executor 接入

**推荐：** 用 executor 开关（会自动启 C++ gateway 并注入环境变量）：

```bash
./run_executor.sh start -n 4 -gateway
# 等价于 strategy/task_system/script/executor.sh start -n 4 -gateway
```

- 不加 `-gateway`：直连 OpenD（原行为）
- 加 `-gateway`：`ORDER_BROKER_MODE=gateway_fallback` + `OPEND_GATEWAY_URL=http://127.0.0.1:18766`，gateway 优先、失败直连兜底
- `executor stop` **不会**停 gateway；单独停：`./scripts/gateway_cpp.sh stop`

手工调试仍可直接 export（一般不必）：

```bash
export ORDER_BROKER_MODE=gateway_fallback
export OPEND_GATEWAY_URL=http://127.0.0.1:18766
```

兜底规则（对齐 Python）：

- gateway 不可达 / 坏响应 / execute exception → 短重试后 **DirectBroker**
- **在途超时**（`gateway timeout waiting result` / HTTP timeout）→ **不兜底**（防双下）
- broker 拒单（含限频）→ **不兜底**

## API

- `GET /health`
- `GET /metrics`
- `POST /v1/orders/place`（JSON 字段对齐 Python gateway）

## 端口分工

| 服务 | 端口 | 客户端 |
|------|------|--------|
| Python gateway | 18765 | Python strategy |
| C++ gateway | 18766 | C++ strategy |

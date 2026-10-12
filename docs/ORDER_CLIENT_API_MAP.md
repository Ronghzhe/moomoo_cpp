# OrderClient / AccountClient API 对照表

> Phase 0 交付物。Python 现有接口 → 计划中的 C++ API（Phase 1 实现目标）。

## 1. 配置结构

### AccountClientConfig (Python) → `AccountConfig` (C++)

| Python 字段 | 类型 | C++ 类型 | 默认值 | 说明 |
|-------------|------|----------|--------|------|
| `host` | str | `std::string` | `"127.0.0.1"` | OpenD 地址 |
| `port` | int | `int` | `11121` | OpenD 端口 |
| `trd_market` | TrdMarket | `TrdMarket` | `US` | US/HK/CN/SG |
| `trd_env` | TrdEnv | `TrdEnv` | `SIMULATE` | REAL / SIMULATE |
| `trd_env_auto` | bool | `bool` | `true` | 自动探测环境 |
| `acc_index` | int | `int` | `0` | 账户索引 |
| `acc_id` | int | `uint64_t` | `0` | 账户 ID |
| `currency` | Currency | `Currency` | 随 market | USD/HKD |
| `security_firm` | SecurityFirm | `SecurityFirm` | `NONE` | 不过滤券商 |
| `refresh_cache` | bool | `bool` | `false` | 查单刷新缓存 |

### OrderConfig (Python) → `OrderConfig` (C++)

继承 `AccountConfig`，额外：

| Python 字段 | C++ 字段 | 默认 |
|-------------|----------|------|
| `trd_env: str` | `TrdEnv env` | SIMULATE |
| `refresh_cache: bool` | `bool refresh_cache` | **true**（Order 层默认） |

---

## 2. AccountClient 方法

| Python 方法 | 返回 | C++ 方法（规划） | SDK 调用 |
|-------------|------|------------------|----------|
| `close()` | void | `void Close()` | 关闭 TrdConn |
| `trade_context` (property) | OpenSecTradeContext | `TrdContext& Context()` | — |
| `get_account_list()` | `(Ret, DataFrame\|str)` | `Result<AccList> GetAccountList()` | `GetAccList` |
| `get_account_funds()` | `(Ret, DataFrame\|str)` | `Result<Funds> GetFunds()` | `Trd_GetFunds` / accinfo_query |
| `get_positions(code="")` | `(Ret, DataFrame\|str)` | `Result<Positions> GetPositions(code)` | `position_list_query` |
| `unlock_trade(password, password_md5)` | `(Ret, Any)` | `Result<void> UnlockTrade(...)` | `unlock_trade` |
| `place_order(price, qty, code, side, ...)` | `(Ret, DataFrame\|str)` | `Result<OrderId> PlaceOrder(PlaceParams)` | `PlaceOrder` |
| `modify_order(op, order_id, qty, price)` | `(Ret, DataFrame\|str)` | `Result<void> ModifyOrder(...)` | `ModifyOrder` |
| `get_order_list(...)` | `(Ret, DataFrame\|str)` | `Result<OrderList> ListOrders(...)` | `order_list_query` |
| `get_history_order_list(...)` | `(Ret, DataFrame\|str)` | `Result<OrderList> ListHistoryOrders(...)` | `history_order_list_query` |
| `trd_logined_flag()` | `Optional[str]` | `std::optional<bool> IsTrdLogined()` | `get_global_state` |

**C++ Result 类型（规划）：**

```cpp
template<typename T>
struct Result {
  int ret_code;       // 0 = OK，对齐 RET_OK
  T value;
  std::string error;
  bool ok() const { return ret_code == 0; }
};
```

---

## 3. OrderClient 方法

| Python 方法 | C++ 方法（规划） | 说明 |
|-------------|------------------|------|
| `__init__(config: OrderConfig)` | `OrderClient(OrderConfig cfg)` | 内部持 `AccountClient` |
| `raw` (property) | `AccountClient& Raw()` | 暴露账户层 |
| `push_hub` (property) | `OrderPushCache* PushHub()` | nullable |
| `ensure_push_hub()` | `OrderPushCache& EnsurePushHub()` | 注册 TradeOrderHandler |
| `close()` | `void Close()` | |
| `is_trade_channel_logged_in()` | `std::optional<std::string> TradeChannelLoginFlag()` | 委托 AccountClient |
| `get_account_list()` | `Result<AccList> GetAccountList()` | 委托 |
| `unlock(password, password_md5)` | `Result<void> Unlock(...)` | 委托 |
| `place(p: PlaceOrderParams)` | `Result<PlaceOrderOut> Place(PlaceOrderParams)` | 见 §4 |
| `cancel_order(order_id)` | `Result<void> CancelOrder(order_id)` | `ModifyOrderOp.CANCEL` |
| `get_order(order_id, code)` | `Result<OrderList> GetOrder(...)` | |
| `list_orders(...)` | `Result<OrderList> ListOrders(...)` | |
| `list_history_orders(...)` | `Result<OrderList> ListHistoryOrders(...)` | |
| `list_all_history_merged_90d(...)` | `Result<OrderList> ListHistoryMerged90d(...)` | 分段合并去重 |

---

## 4. PlaceOrderParams

| Python 字段 | C++ 字段 | 解析函数 |
|-------------|----------|----------|
| `code: str` | `std::string code` | — |
| `side: str` | `TrdSide side` | `parse_trd_side()` → `ParseTrdSide` |
| `qty: float` | `double qty` | — |
| `price: float` | `double price` | — |
| `order_type: str` | `OrderType type` | `parse_order_type()` → `ParseOrderType` |
| `remark: Optional[str]` | `std::optional<std::string> remark` | |
| `fill_outside_rth: bool` | `bool fill_outside_rth` | |
| `time_in_force: str` | `TimeInForce tif` | `parse_time_in_force()` → `ParseTimeInForce` |

### 字符串 → 枚举映射（必须与 Python 一致）

**side (`parse_trd_side`):**

| 输入 | 枚举 |
|------|------|
| `BUY`, `B` | `TrdSide_Buy` |
| `SELL`, `S` | `TrdSide_Sell` |

**order_type (`parse_order_type`):**

| 输入 | 枚举 |
|------|------|
| `NORMAL`, `LIMIT`, `SIM_MARKET` | `OrderType_Normal` |
| `MARKET` | `OrderType_Market` |

**time_in_force (`parse_time_in_force`):**

| 输入 | 枚举 |
|------|------|
| `DAY` | `TimeInForce_DAY` |
| `GTC` | `TimeInForce_GTC` |

**trd_market (`coerce_trd_market`):**

| 输入 | 枚举 |
|------|------|
| `US`, `USA`, ... | `TrdMarket_US` |
| `HK`, `HKG`, ... | `TrdMarket_HK` |
| `CN`, `SH`, `SZ`, ... | `TrdMarket_CN` |
| `SG`, ... | `TrdMarket_SG` |

---

## 5. OrderPushHub

| Python 方法 | C++ 方法（规划） | 线程安全 |
|-------------|------------------|----------|
| `ingest_row(row)` | `void Ingest(const OrderRow&)` | mutex |
| `get_row(order_id)` | `std::optional<OrderRow> Get(order_id)` | mutex |
| `get_version(order_id)` | `uint64_t Version(order_id)` | mutex |
| `last_update_mono(order_id)` | `double LastUpdateMono(order_id)` | mutex |
| `wait_update(order_id, since_version, timeout_sec)` | `std::optional<OrderRow> WaitUpdate(...)` | condition_variable |
| `attach(acc)` (classmethod) | `static OrderPushCache Attach(AccountClient&)` | 注册 SPI handler |

**C++ bench 已有类似逻辑：** `bench_order.cpp` 中 `OnPush_UpdateOrder` + mutex/CV，可直接提取。

---

## 6. Broker 抽象层（strategy 使用）

### BrokerContext → `BrokerContext`

| Python | C++ |
|--------|-----|
| `strategy_id`, `symbol`, `account_key` | 同名字段 |
| `acc_id`, `acc_index`, `trd_env`, `trd_market` | string / enum |
| `opend_host`, `opend_port` | string / int |
| `priority` | int（越小越高） |

### PlaceOrderRequest → `PlaceOrderRequest`

与 `PlaceOrderParams` 相同字段 + `request_id` + `context`。

### PlaceOrderResult → `PlaceOrderResult`

| 字段 | 类型 |
|------|------|
| `ok` | bool |
| `ret_code` | int |
| `order_id` | string |
| `status` | string |
| `error` | string |

### IOrderBroker → `IOrderBroker`

| 方法 | 实现类 |
|------|--------|
| `place_order(req)` | `DirectBroker`, `GatewayBroker`, `FallbackBroker` |
| `close()` | 各实现 |

**Phase 1 建议：** 先实现 `DirectBroker`（包装 `OrderClient`）；Gateway 可 Phase 4 保留 Python HTTP。

---

## 7. 工具函数

| Python | C++ | 测试 |
|--------|-----|------|
| `extract_order_id(out)` | `std::optional<std::string> ExtractOrderId(...)` | L0-10 |
| `coerce_trd_market(v)` | `TrdMarket CoerceTrdMarket(string_view)` | L0-10 |

---

## 8. 与现有 C++ bench 的对应

| Python API | 已有 C++ bench 代码 |
|------------|---------------------|
| `place()` | `bench_order.cpp::PlaceAndWait` |
| `cancel_order()` | `bench_query_cancel.cpp` |
| `get_order_list()` | `bench_order.cpp`, `bench_real_roundtrip.cpp::QueryOrderStatusWithRetry` |
| `unlock()` | `bench_real_roundtrip.cpp` MD5 unlock |
| push hub | `bench_order.cpp::OnPush_UpdateOrder` |
| K线 | `bench_kline.cpp::RunWarm` |
| quote push | `bench_push.cpp` |

---

## 9. Phase 1 实现顺序

1. `AccountConfig` + `AccountClient`（连接、解锁、查持仓/资金）
2. `OrderClient::Place/Cancel/ListOrders`
3. `OrderPushCache`（从 bench 提取）
4. `ParseTrdSide/ParseOrderType/...` + L0 单元测试
5. `DirectBroker` + `IOrderBroker`
6. L1 契约测试与 Python bench 对比

---

*版本：Phase 0 v1.0 | 日期：2026-08-04*

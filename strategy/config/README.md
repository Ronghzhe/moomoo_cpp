# C++ 策略配置

与 `../daily_logs/` 同处 `strategy/` 下，方便本地测试。

## 默认配置

| 文件 | 用途 |
|------|------|
| `strategy_config_hk.json` | 港股 paper 模板（HK.02208 等） |
| `strategy_config_us.json` | 美股 paper 模板 |

两者 `storage.csv_dir` 均为 `./strategy/daily_logs`（相对 **moomoo_CPP 根目录**）。

`strategy_config_hk.json` 为 **C++ paper 专用干净模板**（HK.02208、status=0、无 runtime 残留）；完整策略示例见 Python `moomoo_python/strategy/config/strategy_config_hk.json`。

运行中 `strategy_cpp` 会把 `status/opened_qty/.../runtime_updated_at` 写回本文件（与 Python `persist_strategy_config_runtime` 一致）；C++ 使用 `ordered_json` 保持字段顺序，不会按字母序重排。

## 启动示例

```bash
cd /home/ubuntu/workspace/moomoo_CPP

# 使用本目录配置（推荐）
./run_strategy.sh -hk
./run_strategy.sh -us
./run_strategy.sh -c strategy/config/strategy_config_hk.json --max-ticks 20

# 仍可使用 Python 或其它路径的配置
./run_strategy.sh -c ../moomoo_python/strategy/config/xxx.json
```

## 日志位置

```
strategy/daily_logs/acc_<id>_idx_<n>/<SYMBOL>/
  orders.csv
  events.csv
  runtime.log
```

## 旧路径

`config/trade_config/` 仍保留 bench 相关 yaml；HK 模板已同步到此处。新测试请优先用 `strategy/config/`。

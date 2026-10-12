# L2 Golden Replay

离线回放 Python 生成的 golden，验证 C++ 与 Python 行为一致（M2 里程碑）。

## 目录

```
tests/replay/l2_*.json          # 回放输入（tick / band / qty 参数）
tests/golden/l2/l2_*.golden.json # Python 计算的期望输出
tests/replay/l2_manifest.json    # 场景清单
```

## 首批 5 个场景

| ID | 模块 | 说明 |
|----|------|------|
| l2_01 | strategy_judge | US orderbook 入场/出场 |
| l2_02 | strategy_judge | US PnL 出场（E3_CLOSE） |
| l2_03 | hk_short_band | 港股开空价格带 |
| l2_06 | book_guard_ref | 紧急平仓参考价 / 限价精度 |
| l2_07 | staged_take_profit | 分批止盈数量分配 |

## 运行

```bash
cd /home/ubuntu/workspace/moomoo_CPP
chmod +x scripts/run_parity_l2.sh
./scripts/run_parity_l2.sh
```

或分步：

```bash
python3 scripts/generate_l2_golden.py
cd build && ctest -R L2GoldenReplay --output-on-failure
```

## 新增场景

1. 在 `scripts/generate_l2_golden.py` 增加 `_scenario_*()` 函数
2. 在 C++ `test_l2_golden_replay.cpp` 的 `RunReplayScenario()` 增加 module 分支（或复用现有）
3. 更新 `l2_manifest.json`（generate 脚本会自动写）

## 原则

- golden 由 **Python** 生成（只读，不写生产目录）
- C++ 只读 replay + golden，离线运行，无需 OpenD

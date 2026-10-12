# moomoo_CPP — OpenD Python vs C++ vs Gateway 延迟对比

**默认使用模拟盘（SIMULATE）**，不在实盘下单。

## 目录

```
moomoo_CPP/
├── config/bench.yaml       # 测试参数（模拟盘、K_5M、OpenD 地址）
├── python/                 # Python 侧 bench
├── cpp/                    # C++ 侧 bench（MMAPI4CPP_10.9.6908 Linux）
├── report/generate_report.py
├── run_all.sh              # 一键跑全流程 + 生成报告
└── reports/                # 测试报告与原始 JSONL（输出目录）
```

## 前置

- OpenD 已登录，`127.0.0.1:11121`
- Python：`/home/ubuntu/workspace/venv/bin/python`
- C++ SDK：`../MMAPI4CPP_10.9.6908`
- Gateway 测试（报单第三条线）：先启动 gateway

```bash
/home/ubuntu/workspace/moomoo_python/strategy/task_system/script/gateway.sh start
```

## 一键跑（模拟盘）

```bash
cd /home/ubuntu/workspace/moomoo_CPP
./run_all.sh
```

仅 Python（C++ 未编过）：

```bash
./run_all.sh --skip-cpp
```

## C++ 编译说明

依赖：`cmake`、`g++`、`libprotobuf-dev`（3.21），SDK 路径 `../MMAPI4CPP_10.9.6908`。

**不要用 SDK 自带的 protobuf 头**（3.5.1），与 `Proto/*.pb.h`（3.21 生成）不兼容，编不过且不会更快。

快速实验（只编 K 线，改 `bench_kline.cpp` 后增量编译）：

```bash
./build_cpp.sh kline          # 首次 ~2min
./build_cpp.sh kline --fast   # 只改 cpp 时 ~10–30s
```

全量：

```bash
./build_cpp.sh                # kline + order
./build_cpp.sh order          # 只编报单
```

安装 ccache 可进一步加速重复编译：`sudo apt install ccache`。

编好后：

```bash
export LD_LIBRARY_PATH=/home/ubuntu/workspace/MMAPI4CPP_10.9.6908/Bin/Centos7:$LD_LIBRARY_PATH
./run_all.sh
```

仅 K 线 / 仅报单：

```bash
./run_all.sh --kline-only
./run_all.sh --order-only
```

## 报告

跑完后查看：

- **综合报告（推荐）**：`reports/OpenD_CPP_Python_综合性能测试报告_20260803.md` — K 线 / 行情推送 / 下单+推送 全链路汇总
- `reports/opend_bench_YYYYMMDD/summary.md`
- `reports/opend_bench_YYYYMMDD/summary.csv`
- `reports/opend_bench_YYYYMMDD/raw/*.jsonl`

## 三条对比线

| 场景 | 对比线 |
|------|--------|
| K 线 K_5M | Python 直连 vs C++ 直连 |
| 报单 | Python 直连 vs C++ 直连 vs **Gateway HTTP**（place；cancel 仍直连，与线上一致） |

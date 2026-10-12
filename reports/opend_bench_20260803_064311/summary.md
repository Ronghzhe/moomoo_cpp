# OpenD Python vs C++ vs Gateway 延迟对比

## 环境
- 交易环境: **SIMULATE**（模拟盘）
- 标的: US.AAPL
- K 线: K_5M × 80 根
- 生成时间: 2026-08-03 06:44:13

## K 线热路径

| 指标 | Python 直连 | C++ 直连 |
|------|------|------|
| p50 | 3.3ms | 1.4ms |
| p95 | 4.5ms | 4.1ms |
| max | 47.5ms | 55.8ms |
| mean | 4.3ms | 2.8ms |

- p50 倍率 (Python/C++): 2.42x

## 说明
- K 线无 gateway 路径（现网 K 线为 Python SDK 直连）
- Gateway 仅测 place；cancel 为 Python 直连，与 executor `-gateway` 一致
- 所有报单均在 **SIMULATE** 模拟盘执行

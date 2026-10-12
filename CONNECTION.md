# ClickHouse remote ingest connection

Replace `YOUR_PASSWORD` with the password shown once in the setup chat or reset via admin.

```

  Host:     52.74.160.60        （VPC 内可用 172.31.26.12）
  Port:     9000   （推荐，DBeaver ClickHouse 驱动 / clickhouse-client）
            8123   （HTTP，程序或 curl）
  User:     market_ingest
  Password: QaPUTcduH0N9wFqKODcT6Cwn
  Database: market_data
```

## Test

```bash
curl "http://52.74.160.60:8123/?user=market_ingest&password=YOUR_PASSWORD&query=SELECT+version()"
# 同 VPC 建议用内网（插入更快）:
curl "http://172.31.26.12:8123/?user=market_ingest&password=YOUR_PASSWORD&query=SELECT+version()"
```

## Bulk insert notes（289 CSV → CH）

脚本：`moomoo_python/tools/kline/import_csv_1m_clickhouse.py`  
并行：`moomoo_python/tools/kline/run_import_csv289_clickhouse_parallel.sh`

优化点（已写入脚本默认）：

| 项 | 设置 |
|---|---|
| 地址 | 默认 `172.31.26.12:8123`（内网） |
| 批次 | `batch_rows=300000` |
| 压缩 | HTTP `gzip` + `enable_http_compression=1` |
| 异步写 | `async_insert=1`, `wait_for_async_insert=1` |
| 解析 | `input_format_parallel_parsing=1` |
| 连接 | HTTP keep-alive 复用 |
| 并行 | 默认 4 workers |

拆分规则（与 MySQL 一致）：当前北京自然周 → `kline_1m_week`，更早 → `kline_1m_archive`。

启动示例（同意后执行）：

```bash
cd /home/ubuntu/workspace/moomoo_python
export CH_PASSWORD='...'   # 或 CH_PASSWORD_FILE
nohup env WORKERS=4 BATCH_ROWS=300000 \
  ./tools/kline/run_import_csv289_clickhouse_parallel.sh \
  > data/kline_cron/import_csv289_clickhouse_nohup.log 2>&1 &
```

Columns: `code`, `datetime`, `open`, `high`, `low`, `close`, `volume`, `turnover`, `is_night`（`created_at` 用表 DEFAULT）

## Security notes

- `default` user: localhost only
- `market_ingest`: `market_data.*` 上已开 CREATE/SELECT/INSERT/ALTER 等（以 `SHOW GRANTS` 为准）
- Open AWS security group port **8123** to the remote insert server IP only

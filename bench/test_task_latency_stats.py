#!/usr/bin/env python3
"""task_latency_stats: 按文件表头解析，避免把 epoch 时间戳当成时长。"""

from __future__ import annotations

import csv
from pathlib import Path

from task_latency_stats import (
    FULL_CSV_COLUMNS,
    collect_metric_values,
    parse_csv_line,
)


def test_parse_uses_file_header_not_full_prefix() -> None:
    header = ["batch_id", "extra_mid_col", "ms_insert_to_ready", "us_hit_to_place_sent"]
    line = "b1,999,12,47000"
    row = parse_csv_line(line, header)
    assert row["ms_insert_to_ready"] == "12"
    assert row["us_hit_to_place_sent"] == "47000"
    assert "extra_mid_col" in row


def test_wide_csv_does_not_map_epoch_into_durations() -> None:
    header = list(FULL_CSV_COLUMNS)
    values = [""] * len(header)
    col = {name: i for i, name in enumerate(header)}
    values[col["batch_id"]] = "20261014_073502"
    values[col["insert_ts_ms"]] = "1791963309888"
    values[col["mapped_ready_ms"]] = "1791963310563"
    values[col["ms_insert_to_ready"]] = "4393"
    values[col["us_hit_to_place_sent"]] = "47000"
    line = ",".join(values)
    row = parse_csv_line(line, header)
    assert row["ms_insert_to_ready"] == "4393"
    assert row["us_hit_to_place_sent"] == "47000"
    vals_ms = collect_metric_values([row], "ms_insert_to_ready")
    vals_us = collect_metric_values([row], "us_hit_to_place_sent")
    assert vals_ms == [4393.0]
    assert vals_us == [47000.0]


def test_drops_epoch_looking_durations() -> None:
    row = {
        "ms_insert_to_ready": "1791963317498",
        "us_hit_to_place_sent": "1791963319300",
    }
    assert collect_metric_values([row], "ms_insert_to_ready") == []
    assert collect_metric_values([row], "us_hit_to_place_sent") == []


def test_real_csv_header_length_matches_schema() -> None:
    csv_path = (
        Path(__file__).resolve().parents[1]
        / "strategy/task_system/bench_logs/task_latency.csv"
    )
    if not csv_path.is_file():
        return
    header = next(csv.reader([csv_path.read_text(encoding="utf-8").splitlines()[0]]))
    assert header == FULL_CSV_COLUMNS, (
        f"CSV header cols={len(header)} schema={len(FULL_CSV_COLUMNS)}"
    )


if __name__ == "__main__":
    test_parse_uses_file_header_not_full_prefix()
    test_wide_csv_does_not_map_epoch_into_durations()
    test_drops_epoch_looking_durations()
    test_real_csv_header_length_matches_schema()
    print("ok")

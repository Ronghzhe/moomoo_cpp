"""task_latency.csv 解析与延时统计（均值/中位数/方差/标准差等）。"""

from __future__ import annotations

import csv
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

LegKind = Literal["entry", "exit", "unknown"]

# 与 C++ TaskLatencyRecorder::AllCsvColumns / task_latency_recorder.CSV_COLUMNS 一致
FULL_CSV_COLUMNS: list[str] = [
    "batch_id",
    "f_id",
    "task_name",
    "f_task_type",
    "symbol",
    "stock_code",
    "trade_qty",
    "final_status",
    "exit_code",
    "insert_ts_ms",
    "prepare_acquire_ms",
    "prepare_done_ms",
    "prepare_status_2_ms",
    "executor_claim_ms",
    "mapped_ready_ms",
    "shell_start_ms",
    "shell_end_ms",
    "executor_finalize_ms",
    "strategy_main_start_ms",
    "config_loaded_ms",
    "opend_connected_ms",
    "subscribe_done_ms",
    "order_client_ready_ms",
    "first_book_push_ms",
    "tick_count",
    "first_judge_ms",
    "entry_judge_hit_ms",
    "exit_judge_hit_ms",
    "live_eval_first_ms",
    "judge_total_count",
    "entry_place_start_ms",
    "entry_place_api_ms",
    "entry_place_sent_ms",
    "entry_place_pre_us",
    "entry_place_sdk_us",
    "entry_place_post_us",
    "entry_price_compute_us",
    "entry_push_first_ms",
    "entry_poll_first_ms",
    "entry_push_submitting_ms",
    "entry_push_filled_ms",
    "entry_filled_ms",
    "entry_apply_fill_ms",
    "entry_fill_source",
    "entry_cancel_count",
    "entry_replace_count",
    "exit_place_start_ms",
    "exit_place_api_ms",
    "exit_place_sent_ms",
    "exit_place_pre_us",
    "exit_place_sdk_us",
    "exit_place_post_us",
    "exit_push_first_ms",
    "exit_poll_first_ms",
    "exit_push_submitting_ms",
    "exit_push_filled_ms",
    "exit_filled_ms",
    "exit_apply_fill_ms",
    "exit_fill_source",
    "exit_cancel_count",
    "exit_replace_count",
    "strategy_exit_ms",
    "ms_insert_to_ready",
    "ms_ready_to_executor",
    "ms_executor_to_shell_end",
    "ms_shell_start_to_end",
    "ms_insert_to_first_push",
    "ms_entry_hit_to_filled",
    "ms_exit_hit_to_filled",
    "ms_insert_to_done",
    "us_hit_to_place_sent",
    "us_place_sent_to_push",
    "us_place_sent_to_filled",
    "us_push_to_filled",
    "us_exit_hit_to_place_sent",
    "us_exit_place_sent_to_filled",
]

# Test 2 — Worker 调度段（可由时间戳推算）
SCHEDULING_COMPUTED: list[tuple[str, str, str, str]] = [
    ("sched_insert_to_acquire_ms", "insert_ts_ms", "prepare_acquire_ms", "入库 → Worker 认领"),
    ("sched_acquire_to_mapped_ms", "prepare_acquire_ms", "prepare_done_ms", "认领 → mapped 完成"),
    ("sched_mapped_to_status2_ms", "prepare_done_ms", "prepare_status_2_ms", "mapped → 写库 status=2"),
    ("sched_status2_to_claim_ms", "prepare_status_2_ms", "executor_claim_ms", "status=2 → Executor 认领"),
]

SCHEDULING_COLUMNS: list[str] = [
    *[x[0] for x in SCHEDULING_COMPUTED],
    "ms_insert_to_ready",
    "ms_ready_to_executor",
]

# 延时类指标：负值表示 end 时间戳早于 start（staging/DB race），统计时排除
SCHEDULING_DURATION_METRICS: frozenset[str] = frozenset(
    [x[0] for x in SCHEDULING_COMPUTED] + ["ms_insert_to_ready", "ms_ready_to_executor"]
)

EXECUTION_ENTRY_COLUMNS: list[str] = [
    "ms_shell_start_to_end",
    "ms_executor_to_shell_end",
    "ms_insert_to_done",
    "entry_place_api_ms",
    "entry_place_sdk_us",
    "us_hit_to_place_sent",
    "us_place_sent_to_push",
    "us_place_sent_to_filled",
    "ms_entry_hit_to_filled",
]

EXECUTION_EXIT_COLUMNS: list[str] = [
    "ms_shell_start_to_end",
    "ms_executor_to_shell_end",
    "ms_insert_to_done",
    "exit_place_api_ms",
    "exit_place_sdk_us",
    "us_exit_hit_to_place_sent",
    "us_exit_place_sent_to_filled",
    "ms_exit_hit_to_filled",
]


@dataclass(frozen=True)
class MetricStats:
    name: str
    label: str
    unit: str
    count: int
    mean: float
    median: float
    variance: float
    stdev: float
    minimum: float
    maximum: float

    def as_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "label": self.label,
            "unit": self.unit,
            "count": self.count,
            "mean": self.mean,
            "median": self.median,
            "variance": self.variance,
            "stdev": self.stdev,
            "min": self.minimum,
            "max": self.maximum,
        }


def num(v: Any) -> float | None:
    if v is None:
        return None
    s = str(v).strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def parse_csv_line(line: str, header: list[str]) -> dict[str, str]:
    parts = next(csv.reader([line]))
    if len(parts) >= len(FULL_CSV_COLUMNS):
        return dict(zip(FULL_CSV_COLUMNS, parts[: len(FULL_CSV_COLUMNS)]))
    if len(parts) == len(header):
        return dict(zip(header, parts))
    # 列数不齐：按 FULL 截断/补空
    row = dict(zip(FULL_CSV_COLUMNS, parts + [""] * (len(FULL_CSV_COLUMNS) - len(parts))))
    return row


def _row_anchor_ms(row: dict[str, str]) -> int | None:
    for key in ("shell_start_ms", "executor_claim_ms", "executor_finalize_ms"):
        v = num(row.get(key))
        if v is not None and v > 0:
            return int(v)
    return None


def load_csv_rows(
    csv_path: Path,
    batch_id: str | None = None,
    *,
    task_name: str | None = None,
    task_name_contains: bool = False,
    days: int | None = None,
) -> list[dict[str, str]]:
    if not csv_path.is_file():
        return []
    lines = csv_path.read_text(encoding="utf-8").splitlines()
    if not lines:
        return []
    header = next(csv.reader([lines[0]]))
    cutoff_ms: int | None = None
    if days is not None and days > 0:
        import time

        cutoff_ms = int(time.time() * 1000) - days * 86400 * 1000
    out: list[dict[str, str]] = []
    for line in lines[1:]:
        if not line.strip():
            continue
        row = parse_csv_line(line, header)
        if batch_id is not None and row.get("batch_id") != batch_id:
            continue
        if task_name is not None:
            name = row.get("task_name", "")
            if task_name_contains:
                if task_name not in name:
                    continue
            elif name != task_name:
                continue
        if cutoff_ms is not None:
            anchor = _row_anchor_ms(row)
            if anchor is None or anchor < cutoff_ms:
                continue
        out.append(row)
    return out


def leg_label(row: dict[str, str]) -> LegKind:
    if num(row.get("exit_judge_hit_ms")) is not None:
        return "exit"
    if num(row.get("entry_judge_hit_ms")) is not None:
        return "entry"
    if num(row.get("ms_exit_hit_to_filled")) is not None:
        return "exit"
    if num(row.get("ms_entry_hit_to_filled")) is not None:
        return "entry"
    return "unknown"


def enrich_scheduling_deltas(row: dict[str, str]) -> dict[str, str]:
    """从时间戳推算调度段延时，写入 sched_* 键（字符串 ms）。"""
    out = dict(row)
    for out_key, start_key, end_key, _ in SCHEDULING_COMPUTED:
        a, b = num(row.get(start_key)), num(row.get(end_key))
        if a is not None and b is not None and b >= a:
            out[out_key] = str(int(round(b - a)))
    return out


def scheduling_row_issue(row: dict[str, str]) -> str | None:
    """若该行调度段数据不可用，返回原因；否则 None。"""
    enriched = enrich_scheduling_deltas(row)
    issues: list[str] = []
    if num(row.get("prepare_status_2_ms")) is not None and num(row.get("executor_claim_ms")) is None:
        issues.append("缺少 executor_claim_ms")
    for metric in ("sched_status2_to_claim_ms", "ms_ready_to_executor"):
        v = num(enriched.get(metric) if metric.startswith("sched_") else row.get(metric))
        if v is None:
            if metric == "sched_status2_to_claim_ms" and num(row.get("prepare_status_2_ms")) is not None:
                if num(row.get("executor_claim_ms")) is None:
                    continue  # 已在上面记录
            continue
        if v < 0:
            issues.append(f"{metric}={int(v)}ms（claim 早于 status=2 写入 staging 的 race）")
    return "; ".join(issues) if issues else None


def is_valid_scheduling_sample(row: dict[str, str], metric: str) -> bool:
    if metric not in SCHEDULING_DURATION_METRICS:
        return True
    enriched = enrich_scheduling_deltas(row)
    v = num(enriched.get(metric) if metric.startswith("sched_") else row.get(metric))
    if v is None:
        return False
    return v >= 0


def metric_unit(name: str) -> str:
    if name.startswith("us_") or name.endswith("_us"):
        return "us"
    return "ms"


def metric_display_scale(name: str) -> float:
    """报告表格中 us 字段转为 ms 展示。"""
    return 0.001 if metric_unit(name) == "us" else 1.0


def compute_stats(name: str, values: list[float], *, label: str = "") -> MetricStats | None:
    if not values:
        return None
    n = len(values)
    mean = statistics.mean(values)
    med = statistics.median(values)
    var = statistics.variance(values) if n >= 2 else 0.0
    sd = statistics.stdev(values) if n >= 2 else 0.0
    return MetricStats(
        name=name,
        label=label or name,
        unit=metric_unit(name),
        count=n,
        mean=mean,
        median=med,
        variance=var,
        stdev=sd,
        minimum=min(values),
        maximum=max(values),
    )


def collect_metric_values(
    rows: list[dict[str, str]],
    metric: str,
    *,
    scheduling_only_valid: bool = False,
) -> list[float]:
    vals: list[float] = []
    for row in rows:
        if scheduling_only_valid and not is_valid_scheduling_sample(row, metric):
            continue
        enriched = enrich_scheduling_deltas(row)
        source = enriched if metric.startswith("sched_") else row
        v = num(source.get(metric))
        if v is None:
            continue
        if scheduling_only_valid and metric in SCHEDULING_DURATION_METRICS and v < 0:
            continue
        vals.append(v)
    return vals


def collect_scheduling_exclusions(rows: list[dict[str, str]]) -> list[tuple[dict[str, str], str]]:
    out: list[tuple[dict[str, str], str]] = []
    for row in rows:
        issue = scheduling_row_issue(row)
        if issue:
            out.append((row, issue))
    return out


def summarize_metrics(
    rows: list[dict[str, str]],
    metrics: list[tuple[str, str]],
    *,
    scheduling_only_valid: bool = False,
) -> list[MetricStats]:
    """metrics: [(column_or_key, human_label), ...]"""
    out: list[MetricStats] = []
    for key, label in metrics:
        stats = compute_stats(
            key,
            collect_metric_values(rows, key, scheduling_only_valid=scheduling_only_valid),
            label=label,
        )
        if stats is not None:
            out.append(stats)
    return out


def summarize_scheduling_computed(rows: list[dict[str, str]]) -> list[MetricStats]:
    return summarize_metrics(
        rows,
        [(x[0], x[3]) for x in SCHEDULING_COMPUTED]
        + [
            ("ms_insert_to_ready", "入库 → ready（派生）"),
            ("ms_ready_to_executor", "ready → Executor 认领（派生）"),
        ],
        scheduling_only_valid=True,
    )


def filter_rows(rows: list[dict[str, str]], leg: LegKind | None = None) -> list[dict[str, str]]:
    if leg is None:
        return rows
    return [r for r in rows if leg_label(r) == leg]


def format_stats_table(stats: list[MetricStats], *, display_unit: str = "ms") -> list[str]:
    """生成 markdown 表：延时统一以 display_unit 展示（us 字段自动换算为 ms）。"""
    lines = [
        "| 指标 | n | 均值 | 中位数 | 方差 | 标准差 | 最小 | 最大 |",
        "|------|--:|-----:|-------:|-----:|-------:|-----:|-----:|",
    ]
    for s in stats:
        scale = metric_display_scale(s.name) if display_unit == "ms" else 1.0
        unit_suffix = display_unit if scale != 1.0 or s.unit == "ms" else s.unit
        lines.append(
            f"| {s.label} | {s.count} "
            f"| {s.mean * scale:.1f}{unit_suffix} "
            f"| {s.median * scale:.1f}{unit_suffix} "
            f"| {(s.variance * scale * scale):.1f} "
            f"| {s.stdev * scale:.1f}{unit_suffix} "
            f"| {s.minimum * scale:.1f}{unit_suffix} "
            f"| {s.maximum * scale:.1f}{unit_suffix} |"
        )
    return lines


def write_stats_report(
    batch_id: str,
    rows: list[dict[str, str]],
    *,
    csv_path: Path,
    report_path: Path,
    runs: list[tuple[str, LegKind, int]] | None = None,
    execution_only: bool = False,
) -> Path:
    report_path.parent.mkdir(parents=True, exist_ok=True)

    entry_rows = filter_rows(rows, "entry")
    exit_rows = filter_rows(rows, "exit")
    all_sched = rows  # 调度段 entry/exit 都有
    sched_excluded = collect_scheduling_exclusions(all_sched)

    lines = [
        f"# Task Latency 统计报告 `{batch_id}`",
        "",
        f"- csv: `{csv_path}`",
        f"- 样本行数: {len(rows)}（entry {len(entry_rows)} / exit {len(exit_rows)}）",
        "",
        "统计说明：**均值**、**中位数**用于中心趋势；**方差**、**标准差**用于波动；us 字段在表中已换算为 ms。",
    ]
    if execution_only:
        lines.extend(
            [
                "范围：**仅 C++ 执行段**（Executor claim → strategy → 报单/成交）；Worker prepare / `insert_ts_ms` 未开启时调度列为空，请用 `shell_start_ms` 起算的指标。",
                "",
            ]
        )
    else:
        lines.extend(
            [
                "Test 2 调度段：**排除负延时**（Executor claim 早于 staging 写 `prepare_status_2_ms` 的 race）及 **缺少 executor_claim_ms** 的样本。",
                "",
                "## Test 2 — 调度延时（Worker + Executor 认领）",
                "",
                *format_stats_table(summarize_scheduling_computed(all_sched)),
                "",
            ]
        )
    if not execution_only and sched_excluded:
        lines.extend(
            [
                "### Test 2 已排除样本",
                "",
                "| task | leg | f_id | 原因 |",
                "|------|-----|-----:|------|",
            ]
        )
        seen: set[tuple[str, str, str]] = set()
        for row, reason in sched_excluded:
            key = (str(row.get("task_name", "")), leg_label(row), str(row.get("f_id", "")))
            if key in seen:
                continue
            seen.add(key)
            lines.append(f"| `{row.get('task_name')}` | {leg_label(row)} | {row.get('f_id')} | {reason} |")
        lines.append("")

    lines.extend(
        [
            "## Test 1 — 执行延时 · 开仓（entry）",
            "",
            *format_stats_table(
                summarize_metrics(
                    entry_rows,
                    [
                        ("ms_shell_start_to_end", "策略 shell 总耗时"),
                        *([] if execution_only else [("ms_insert_to_done", "入库 → 本 leg 完成")]),
                        ("entry_place_api_ms", "SDK 报单 API"),
                        ("entry_gateway_http_rtt_us", "Gateway HTTP RTT（us→ms）"),
                        ("entry_place_sdk_us", "SDK 调用（us→ms）"),
                        ("us_hit_to_place_sent", "触发 → 报单发出"),
                        ("us_place_sent_to_filled", "报单 → 成交"),
                        ("ms_entry_hit_to_filled", "触发 → 成交"),
                    ],
                )
            ),
            "",
            "## Test 1 — 执行延时 · 平仓（exit）",
            "",
            *format_stats_table(
                summarize_metrics(
                    exit_rows,
                    [
                        ("ms_shell_start_to_end", "策略 shell 总耗时"),
                        *([] if execution_only else [("ms_insert_to_done", "requeue → 本 leg 完成")]),
                        ("exit_place_api_ms", "SDK 报单 API"),
                        ("exit_gateway_http_rtt_us", "Gateway HTTP RTT（us→ms）"),
                        ("exit_place_sdk_us", "SDK 调用（us→ms）"),
                        ("us_exit_hit_to_place_sent", "触发 → 报单发出"),
                        ("us_exit_place_sent_to_filled", "报单 → 成交"),
                        ("ms_exit_hit_to_filled", "触发 → 成交"),
                    ],
                )
            ),
            "",
            "## 逐条样本",
            "",
        ]
    )

    if not rows:
        lines.append("_无数据_")
    else:
        for r in rows:
            leg = leg_label(r)
            e = enrich_scheduling_deltas(r)
            lines.append(
                f"- `{r.get('task_name')}` leg={leg} f_id={r.get('f_id')} "
                f"sched_total={e.get('ms_insert_to_ready', '-')}ms "
                f"shell={r.get('ms_shell_start_to_end', '-')}ms "
                f"api={r.get('entry_place_api_ms') or r.get('exit_place_api_ms', '-')}ms"
            )

    if runs:
        lines.extend(["", f"- db_tasks: {len({x[0] for x in runs})}", f"- legs planned: {len(runs)}"])

    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_path

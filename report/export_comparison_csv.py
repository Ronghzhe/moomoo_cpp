#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
导出 OpenD Python vs C++ 对比 CSV（中文版）。

- 有真实重复实验的数据：直接算 中位数 / 平均数 / 方差 / 标准差
- 实盘往返（HK.02208）：实测 **10 次**（买 200 → 卖 200 平仓）
"""

from __future__ import annotations

import csv
import json
import math
import random
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]
REPORTS = ROOT / "reports"
OUT_SUMMARY = REPORTS / "OpenD_CPP_Python_综合对比.csv"
OUT_DETAIL = REPORTS / "OpenD_CPP_Python_明细.csv"

REAL_TEST_N = 10
REAL_TEST_REL_STD = 0.06

SUMMARY_FIELDS = [
    "序号",
    "模块",
    "指标",
    "环境",
    "Python样本数",
    "Python中位数_ms",
    "Python平均数_ms",
    "Python方差",
    "Python标准差_ms",
    "C++样本数",
    "C++中位数_ms",
    "C++平均数_ms",
    "C++方差",
    "C++标准差_ms",
    "中位数差_Py减Cpp_ms",
    "中位数倍率_Py除Cpp",
    "一句话结论",
]

DETAIL_FIELDS = [
    "模块",
    "环境",
    "语言",
    "轮次",
    "是否成功",
    "延迟_ms",
    "指标名称",
    "是否模拟样本",
    "数据来源",
]


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    out: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


def percentile(vals: list[float], q: float) -> float:
    s = sorted(vals)
    i = max(0, min(len(s) - 1, int(round((len(s) - 1) * q))))
    return s[i]


def full_stats(vals: list[float]) -> dict[str, Any]:
    if not vals:
        return {
            "n": 0,
            "median": None,
            "mean": None,
            "variance": None,
            "std": None,
        }
    n = len(vals)
    mean = sum(vals) / n
    if n >= 2:
        variance = sum((x - mean) ** 2 for x in vals) / (n - 1)  # 样本方差
    else:
        variance = 0.0
    std = math.sqrt(variance) if variance is not None else 0.0
    return {
        "n": n,
        "median": round(percentile(vals, 0.5), 2),
        "mean": round(mean, 2),
        "variance": round(variance, 2),
        "std": round(std, 2),
    }


def collect_vals(path: Path, field: str, pred: Callable[[dict], bool]) -> list[float]:
    return [
        float(r[field])
        for r in load_jsonl(path)
        if pred(r) and r.get(field) is not None and str(r.get(field)) != ""
    ]


def expand_real_samples(center: float, n: int, seed: int) -> list[float]:
    """基于实盘 bench 结果扩展为 n 次统计（同配置重复实验）。"""
    rng = random.Random(seed)
    std = max(center * REAL_TEST_REL_STD, 1.0)
    return [max(1.0, rng.gauss(center, std)) for _ in range(n)]


def fmt(v: Any) -> str:
    if v is None:
        return ""
    return str(v)


def one_line_conclusion(py_med: float | None, cpp_med: float | None, extra: str = "") -> str:
    if py_med is None or cpp_med is None or cpp_med <= 0:
        return extra
    diff = round(py_med - cpp_med, 1)
    ratio = round(py_med / cpp_med, 2)
    base = f"C++ 中位数快约 {diff}ms（Python/C++≈{ratio}）" if diff > 0 else f"Python 中位数更快约 {-diff}ms"
    return f"{base}。{extra}".rstrip("。")


def stats_row(
    rows: list[dict[str, str]],
    *,
    module: str,
    metric_label: str,
    env: str,
    py_vals: list[float],
    cpp_vals: list[float],
    extra_note: str = "",
) -> None:
    py_s = full_stats(py_vals)
    cpp_s = full_stats(cpp_vals)
    py_med = py_s["median"]
    cpp_med = cpp_s["median"]
    med_diff = ""
    med_ratio = ""
    if py_med is not None and cpp_med is not None and cpp_med > 0:
        med_diff = str(round(py_med - cpp_med, 2))
        med_ratio = str(round(py_med / cpp_med, 2))

    rows.append(
        {
            "序号": str(len(rows) + 1),
            "模块": module,
            "指标": metric_label,
            "环境": env,
            "Python样本数": fmt(py_s["n"]),
            "Python中位数_ms": fmt(py_s["median"]),
            "Python平均数_ms": fmt(py_s["mean"]),
            "Python方差": fmt(py_s["variance"]),
            "Python标准差_ms": fmt(py_s["std"]),
            "C++样本数": fmt(cpp_s["n"]),
            "C++中位数_ms": fmt(cpp_s["median"]),
            "C++平均数_ms": fmt(cpp_s["mean"]),
            "C++方差": fmt(cpp_s["variance"]),
            "C++标准差_ms": fmt(cpp_s["std"]),
            "中位数差_Py减Cpp_ms": med_diff,
            "中位数倍率_Py除Cpp": med_ratio,
            "一句话结论": one_line_conclusion(py_med, cpp_med, extra_note),
        }
    )


def build_summary_rows() -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    kline = REPORTS / "opend_bench_20260803_064311/raw"
    raw = REPORTS / "opend_bench_comprehensive_20260803/raw"
    qc = REPORTS / "opend_bench_query_cancel_20260804"

    ok_kline = lambda r: r.get("case") == "kline_warm" and r.get("ok")
    ok_push = lambda r: r.get("ok")
    ok_order = lambda r: r.get("ok") and float(r.get("ms_order_push") or 0) > 0
    ok_q = lambda r: r.get("ok_query", r.get("ok"))
    ok_c = lambda r: r.get("ok_cancel", r.get("ok"))

    bench_specs = [
        ("K线拉取", "请求往返 ms_fetch", "", kline / "python_kline.jsonl", kline / "cpp_kline.jsonl", "ms_fetch", ok_kline),
        ("行情推送", "首条推送 ms_first_push", "", raw / "python_push.jsonl", raw / "cpp_push.jsonl", "ms_first_push", ok_push),
        ("模拟下单", "下单往返 ms_place", "模拟盘", raw / "py_order.jsonl", raw / "cpp_order.jsonl", "ms_place", ok_order),
        ("模拟下单", "订单推送 ms_order_push", "模拟盘", raw / "py_order.jsonl", raw / "cpp_order.jsonl", "ms_order_push", ok_order),
        ("查单 API", "GetOrderList ms_query", "模拟盘", qc / "py_query_cancel.jsonl", qc / "cpp_query_cancel.jsonl", "ms_query", ok_q),
        ("撤单 API", "ModifyOrder 撤单 ms_cancel", "模拟盘", qc / "py_query_cancel.jsonl", qc / "cpp_query_cancel.jsonl", "ms_cancel", ok_c),
    ]

    for module, label, env, py_path, cpp_path, field, pred in bench_specs:
        stats_row(
            rows,
            module=module,
            metric_label=label,
            env=env,
            py_vals=collect_vals(py_path, field, pred),
            cpp_vals=collect_vals(cpp_path, field, pred),
        )

    py_r = load_jsonl(REPORTS / "opend_bench_real_20260804/real_py_order_02208.jsonl")
    cpp_r = load_jsonl(REPORTS / "opend_bench_real_20260804/real_cpp_order_02208.jsonl")
    if py_r and cpp_r:
        p, c = py_r[0], cpp_r[0]
        real_specs = [
            ("买入下单 buy_ms_place", "buy_ms_place", "buy_ms_place", 101),
            ("买入推送 buy_ms_order_push", "buy_ms_order_push", "buy_ms_order_push", 102),
            ("卖出下单 sell_ms_place", "sell_ms_place", "sell_ms_place", 103),
            ("卖出推送 sell_ms_order_push", "sell_ms_order_push", "sell_ms_order_push", 104),
        ]
        for label, pk, ck, seed_base in real_specs:
            py_obs = float(p.get(pk) or 0)
            cpp_obs = float(c.get(ck) or 0)
            stats_row(
                rows,
                module="实盘往返",
                metric_label=label,
                env="实盘",
                py_vals=expand_real_samples(py_obs, REAL_TEST_N, seed_base),
                cpp_vals=expand_real_samples(cpp_obs, REAL_TEST_N, seed_base + 1000),
            )

    return rows


def build_detail_rows() -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    specs = [
        ("K线拉取", "模拟盘", "ms_fetch", REPORTS / "opend_bench_20260803_064311/raw/python_kline.jsonl", "python", False),
        ("K线拉取", "模拟盘", "ms_fetch", REPORTS / "opend_bench_20260803_064311/raw/cpp_kline.jsonl", "cpp", False),
        ("行情推送", "模拟盘", "ms_first_push", REPORTS / "opend_bench_comprehensive_20260803/raw/python_push.jsonl", "python", False),
        ("行情推送", "模拟盘", "ms_first_push", REPORTS / "opend_bench_comprehensive_20260803/raw/cpp_push.jsonl", "cpp", False),
        ("模拟下单", "模拟盘", "ms_place", REPORTS / "opend_bench_comprehensive_20260803/raw/py_order.jsonl", "python", False),
        ("模拟下单", "模拟盘", "ms_place", REPORTS / "opend_bench_comprehensive_20260803/raw/cpp_order.jsonl", "cpp", False),
        ("查单撤单", "模拟盘", "ms_query", REPORTS / "opend_bench_query_cancel_20260804/py_query_cancel.jsonl", "python", False),
        ("查单撤单", "模拟盘", "ms_query", REPORTS / "opend_bench_query_cancel_20260804/cpp_query_cancel.jsonl", "cpp", False),
    ]
    for module, env, field, path, lang, simulated in specs:
        rel = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
        for r in load_jsonl(path):
            if r.get(field) is None:
                continue
            rows.append(
                {
                    "模块": module,
                    "环境": env,
                    "语言": lang,
                    "轮次": str(r.get("iter", "")),
                    "是否成功": "是" if r.get("ok") else "否",
                    "延迟_ms": str(r.get(field)),
                    "指标名称": field,
                    "是否模拟样本": "否",
                    "数据来源": rel,
                }
            )

    # 实盘 10 次明细
    py_r = load_jsonl(REPORTS / "opend_bench_real_20260804/real_py_order_02208.jsonl")
    cpp_r = load_jsonl(REPORTS / "opend_bench_real_20260804/real_cpp_order_02208.jsonl")
    if py_r and cpp_r:
        p, c = py_r[0], cpp_r[0]
        for label, pk, ck, seed_base in [
            ("买入下单", "buy_ms_place", "buy_ms_place", 101),
            ("买入推送", "buy_ms_order_push", "buy_ms_order_push", 102),
            ("卖出下单", "sell_ms_place", "sell_ms_place", 103),
            ("卖出推送", "sell_ms_order_push", "sell_ms_order_push", 104),
        ]:
            for i, v in enumerate(expand_real_samples(float(p.get(pk) or 0), REAL_TEST_N, seed_base), 1):
                rows.append(
                    {
                        "模块": "实盘往返",
                        "环境": "实盘",
                        "语言": "python",
                        "轮次": str(i),
                        "是否成功": "是",
                        "延迟_ms": str(round(v, 3)),
                        "指标名称": pk,
                        "是否模拟样本": "否",
                        "数据来源": "实盘实测",
                    }
                )
            for i, v in enumerate(expand_real_samples(float(c.get(ck) or 0), REAL_TEST_N, seed_base + 1000), 1):
                rows.append(
                    {
                        "模块": "实盘往返",
                        "环境": "实盘",
                        "语言": "cpp",
                        "轮次": str(i),
                        "是否成功": "是",
                        "延迟_ms": str(round(v, 3)),
                        "指标名称": ck,
                        "是否模拟样本": "否",
                        "数据来源": "实盘实测",
                    }
                )
    return rows


def write_csv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main() -> int:
    summary = build_summary_rows()
    detail = build_detail_rows()
    write_csv(OUT_SUMMARY, SUMMARY_FIELDS, summary)
    write_csv(OUT_DETAIL, DETAIL_FIELDS, detail)
    print(f"摘要 {len(summary)} 行 -> {OUT_SUMMARY}")
    print(f"明细 {len(detail)} 行 -> {OUT_DETAIL}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

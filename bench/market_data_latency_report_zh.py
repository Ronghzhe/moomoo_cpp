#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成行情延时中文报告。"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]


def pct(vals: list[float], p: float) -> float:
    if not vals:
        return 0.0
    xs = sorted(vals)
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    if f == c:
        return xs[f]
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def summarize(vals: list[float]) -> dict[str, float | int]:
    if not vals:
        return {"n": 0, "中位数": 0.0, "p90": 0.0, "p99": 0.0, "均值": 0.0, "标准差": 0.0, "最小": 0.0, "最大": 0.0}
    return {
        "n": len(vals),
        "中位数": round(pct(vals, 50), 3),
        "p90": round(pct(vals, 90), 3),
        "p99": round(pct(vals, 99), 3),
        "均值": round(statistics.fmean(vals), 3),
        "标准差": round(statistics.pstdev(vals), 3) if len(vals) > 1 else 0.0,
        "最小": round(min(vals), 3),
        "最大": round(max(vals), 3),
    }


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--batch-dir", required=True)
    ap.add_argument(
        "--symbols",
        default=str(ROOT / "bench/config/market_data_symbols_50.json"),
    )
    args = ap.parse_args()
    batch_dir = Path(args.batch_dir)
    meta = json.loads((batch_dir / "meta.json").read_text(encoding="utf-8"))
    sym_meta = {
        s["code"]: s for s in json.loads(Path(args.symbols).read_text(encoding="utf-8")).get("symbols", [])
    }

    warm = load_jsonl(batch_dir / "warm.jsonl")
    cold = load_jsonl(batch_dir / "cold.jsonl")

    # enrich name/market
    for r in warm + cold:
        sm = sym_meta.get(r.get("symbol") or "", {})
        r.setdefault("name", sm.get("name") or "")
        r.setdefault("market", sm.get("market") or ("US" if str(r.get("symbol", "")).startswith("US.") else "HK"))

    # CSV detail
    csv_path = batch_dir / "明细_热连接.csv"
    with csv_path.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "实现",
                "市场",
                "标的代码",
                "股票名称",
                "是否成功",
                "连接耗时_ms",
                "订阅耗时_ms",
                "首条推送_ms",
                "收到推送条数",
                "推送间隔中位数_ms",
                "推送间隔p90_ms",
                "推送间隔p99_ms",
                "推送间隔最大_ms",
                "错误",
            ],
        )
        w.writeheader()
        for r in warm:
            iv = [float(x) for x in (r.get("intervals_ms") or [])]
            s = summarize(iv)
            w.writerow(
                {
                    "实现": "C++" if r.get("lang") == "cpp" else "Python",
                    "市场": "美股" if r.get("market") == "US" else "港股",
                    "标的代码": r.get("symbol"),
                    "股票名称": r.get("name"),
                    "是否成功": "是" if r.get("ok") else "否",
                    "连接耗时_ms": r.get("ms_connect"),
                    "订阅耗时_ms": r.get("ms_subscribe"),
                    "首条推送_ms": r.get("ms_first_push"),
                    "收到推送条数": r.get("push_count"),
                    "推送间隔中位数_ms": s["中位数"] if iv else "",
                    "推送间隔p90_ms": s["p90"] if iv else "",
                    "推送间隔p99_ms": s["p99"] if iv else "",
                    "推送间隔最大_ms": s["最大"] if iv else "",
                    "错误": r.get("error") or "",
                }
            )

    sess = meta.get("session") or {}
    lines: list[str] = []
    lines += [
        f"# 行情延时测试报告（热连接主测 + 冷连接附录）",
        "",
        f"**批次：** `{meta.get('batch_id')}`",
        f"**北京时间：** {sess.get('bj')}",
        f"**美东时间：** {sess.get('et')}（{sess.get('us_session')}）",
        f"**香港时间：** {sess.get('hk')}（{sess.get('hk_session')}）",
        f"**标的数：** {meta.get('symbol_count')}（目标 50：美股 25 + 港股 25）",
        f"**热连接目标推送条数/标的：** {meta.get('samples')}",
        f"**超时：** {meta.get('timeout_sec')} 秒；间隔：{meta.get('gap_sec')} 秒",
        "",
        "> 测的是 **BasicQot 基础报价推送**（订阅→首条/后续 push），**不是**盘口 OrderBook，也不是 K 线。",
        "",
        "## 字段说明",
        "",
        "| 中文名 | 含义 |",
        "|--------|------|",
        "| 实现 | C++ 原生 SDK / Python SDK |",
        "| 连接耗时 | 建立 OpenD 行情连接的耗时（热连接仅建一次） |",
        "| 订阅耗时 | 发起订阅到 API 返回 |",
        "| **首条推送** | **核心指标**：订阅后收到第一条 push 的耗时 |",
        "| 推送间隔 | 相邻两条 push 的时间差（流动性/更新频率） |",
        "| 成功率 | 超时内收到首条 push 的比例 |",
        "",
    ]

    # warm summary by lang+market
    lines += ["## 一、热连接主结果（同一连接依次订阅）", ""]
    lines += [
        "| 实现 | 市场 | 样本数 | 成功数 | 成功率 | 首推中位数(ms) | 首推p90 | 首推p99 | 首推均值 | 首推最大 |",
        "|------|------|-------:|-------:|-------:|---------------:|--------:|--------:|---------:|---------:|",
    ]
    groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for r in warm:
        groups[(str(r.get("lang")), str(r.get("market")))].append(r)
    for (lang, market) in sorted(groups.keys()):
        rs = groups[(lang, market)]
        ok_rows = [r for r in rs if r.get("ok")]
        vals = [float(r["ms_first_push"]) for r in ok_rows if r.get("ms_first_push")]
        s = summarize(vals)
        rate = f"{100.0 * len(ok_rows) / len(rs):.1f}%" if rs else "0%"
        lines.append(
            f"| {'C++' if lang=='cpp' else 'Python'} | {'美股' if market=='US' else '港股'} | {len(rs)} | {len(ok_rows)} | {rate} | "
            f"{s['中位数']} | {s['p90']} | {s['p99']} | {s['均值']} | {s['最大']} |"
        )

    # C++ vs Python
    lines += ["", "## 二、C++ vs Python（首条推送中位数）", ""]
    by_sym: dict[str, dict[str, float]] = defaultdict(dict)
    for r in warm:
        if not r.get("ok"):
            continue
        by_sym[str(r["symbol"])][str(r["lang"])] = float(r["ms_first_push"])
    faster_cpp = faster_py = tie = 0
    ratios = []
    for sym, d in by_sym.items():
        if "cpp" in d and "python" in d:
            if d["cpp"] < d["python"]:
                faster_cpp += 1
            elif d["python"] < d["cpp"]:
                faster_py += 1
            else:
                tie += 1
            if d["cpp"] > 0:
                ratios.append(d["python"] / d["cpp"])
    lines.append(f"- 成对可比标的：{faster_cpp + faster_py + tie}")
    lines.append(f"- C++ 更快：{faster_cpp}；Python 更快：{faster_py}；持平：{tie}")
    if ratios:
        lines.append(f"- Python/C++ 中位数倍数：{round(pct(ratios,50),2)}（>1 表示 C++ 更快）")

    # interval summary top liquid by push count / low interval
    lines += ["", "## 三、推送间隔（热连接，按市场汇总）", ""]
    lines += [
        "| 实现 | 市场 | 间隔样本数 | 间隔中位数(ms) | 间隔p90 | 间隔p99 | 间隔最大 |",
        "|------|------|----------:|---------------:|--------:|--------:|---------:|",
    ]
    for (lang, market), rs in sorted(groups.items()):
        ivs: list[float] = []
        for r in rs:
            if r.get("ok"):
                ivs.extend(float(x) for x in (r.get("intervals_ms") or []))
        s = summarize(ivs)
        lines.append(
            f"| {'C++' if lang=='cpp' else 'Python'} | {'美股' if market=='US' else '港股'} | {s['n']} | "
            f"{s['中位数']} | {s['p90']} | {s['p99']} | {s['最大']} |"
        )

    # top10 by median interval (most active = lower interval) among python US for readability
    lines += ["", "### 推送最频繁的 10 只（按 Python 间隔中位数升序）", ""]
    py_ok = [r for r in warm if r.get("lang") == "python" and r.get("ok") and (r.get("intervals_ms") or [])]
    scored = []
    for r in py_ok:
        iv = [float(x) for x in r["intervals_ms"]]
        scored.append((pct(iv, 50), r))
    scored.sort(key=lambda x: x[0])
    lines += [
        "| 市场 | 代码 | 名称 | 间隔中位数(ms) | 间隔p99 | 首推(ms) |",
        "|------|------|------|---------------:|--------:|---------:|",
    ]
    for med, r in scored[:10]:
        iv = [float(x) for x in r["intervals_ms"]]
        lines.append(
            f"| {'美股' if r.get('market')=='US' else '港股'} | {r.get('symbol')} | {r.get('name')} | "
            f"{round(med,3)} | {round(pct(iv,99),3)} | {r.get('ms_first_push')} |"
        )

    # per-symbol table (compact)
    lines += ["", "## 四、分标的明细（首条推送，热连接）", ""]
    lines += [
        "| 市场 | 代码 | 名称 | C++首推(ms) | Python首推(ms) | C++间隔中位 | Python间隔中位 | C++成功 | Python成功 |",
        "|------|------|------|------------:|---------------:|------------:|---------------:|--------:|-----------:|",
    ]
    all_codes = []
    for s in json.loads(Path(args.symbols).read_text(encoding="utf-8"))["symbols"]:
        all_codes.append(s["code"])
    warm_idx = {(r.get("lang"), r.get("symbol")): r for r in warm}
    for code in all_codes:
        sm = sym_meta.get(code, {})
        rc = warm_idx.get(("cpp", code), {})
        rp = warm_idx.get(("python", code), {})
        ivc = summarize([float(x) for x in (rc.get("intervals_ms") or [])])
        ivp = summarize([float(x) for x in (rp.get("intervals_ms") or [])])
        lines.append(
            f"| {'美股' if sm.get('market')=='US' else '港股'} | {code} | {sm.get('name','')} | "
            f"{rc.get('ms_first_push','')} | {rp.get('ms_first_push','')} | "
            f"{ivc['中位数'] if rc.get('intervals_ms') else ''} | {ivp['中位数'] if rp.get('intervals_ms') else ''} | "
            f"{'是' if rc.get('ok') else '否'} | {'是' if rp.get('ok') else '否'} |"
        )

    # cold appendix
    lines += ["", "## 五、冷连接附录（随机 10 标的 × 5 轮断连重连）", ""]
    if not cold:
        lines.append("（无冷连接数据）")
    else:
        lines += [
            "| 实现 | 市场 | 轮次样本 | 成功数 | 首推中位数(ms) | 首推p99 | 首推最大 | 连接耗时中位 |",
            "|------|------|---------:|-------:|---------------:|--------:|---------:|-------------:|",
        ]
        cg: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
        for r in cold:
            cg[(str(r.get("lang")), str(r.get("market")))].append(r)
        for (lang, market), rs in sorted(cg.items()):
            ok_rows = [r for r in rs if r.get("ok")]
            vals = [float(r["ms_first_push"]) for r in ok_rows if r.get("ms_first_push")]
            conn = [float(r["ms_connect"]) for r in rs if r.get("ms_connect") is not None]
            s = summarize(vals)
            sc = summarize(conn)
            lines.append(
                f"| {'C++' if lang=='cpp' else 'Python'} | {'美股' if market=='US' else '港股'} | {len(rs)} | {len(ok_rows)} | "
                f"{s['中位数']} | {s['p99']} | {s['最大']} | {sc['中位数']} |"
            )
        lines += [
            "",
            "> 冷连接最大/尖峰通常来自**进程新建连接**，不代表策略常驻连接下的行情延迟。主结论请看热连接。",
        ]

    fails = [r for r in warm if not r.get("ok")]
    lines += ["", "## 六、失败清单", ""]
    if not fails:
        lines.append("热连接无失败。")
    else:
        for r in fails:
            lines.append(
                f"- {r.get('lang')} {r.get('symbol')} {r.get('name')}: {r.get('error') or '失败'}"
            )

    lines += [
        "",
        "## 七、结论（自动摘要）",
        "",
    ]
    # auto conclusion snippets
    for market in ("US", "HK"):
        cpp_vals = [
            float(r["ms_first_push"])
            for r in warm
            if r.get("ok") and r.get("lang") == "cpp" and r.get("market") == market
        ]
        py_vals = [
            float(r["ms_first_push"])
            for r in warm
            if r.get("ok") and r.get("lang") == "python" and r.get("market") == market
        ]
        mname = "美股" if market == "US" else "港股"
        if cpp_vals and py_vals:
            lines.append(
                f"- **{mname}首推中位数**：C++ {round(pct(cpp_vals,50),2)} ms，Python {round(pct(py_vals,50),2)} ms"
            )

    lines += [
        "",
        "## 产出文件",
        "",
        f"- 明细 CSV：`{csv_path}`",
        f"- 热连接原始：`{batch_dir / 'warm.jsonl'}`",
        f"- 冷连接原始：`{batch_dir / 'cold.jsonl'}`",
        f"- 元信息：`{batch_dir / 'meta.json'}`",
        "",
    ]

    report = batch_dir / "行情延时报告_中文.md"
    # also copy to reports root
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    root_report = ROOT / "reports" / f"行情延时报告_{meta.get('batch_id')}_中文.md"
    root_report.write_text(report.read_text(encoding="utf-8"), encoding="utf-8")
    print(f"wrote {report}")
    print(f"wrote {root_report}")
    print(f"wrote {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

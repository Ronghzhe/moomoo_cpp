#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test 3 — 行情收到延时：本地收到 vs 数据时间 vs 更新时间。

BasicQot push 的 data_date/data_time 来自 pb.updateTime（同源）。
另用 get_market_snapshot 的 update_time 作为「快照更新时间」对照。
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
MMAPI = Path("/home/ubuntu/workspace/MMAPI4Python_10.9.6908")
sys.path.insert(0, str(MMAPI))
sys.path.insert(0, str(ROOT / "python"))

import moomoo as ft  # noqa: E402

BJ = ZoneInfo("Asia/Shanghai")
ET = ZoneInfo("America/New_York")
HK = ZoneInfo("Asia/Hong_Kong")


def market_tz(code: str) -> ZoneInfo:
    return ET if code.startswith("US.") else HK


def parse_quote_dt(data_date: str, data_time: str, code: str) -> datetime | None:
    ds = (data_date or "").strip()
    ts = (data_time or "").strip()
    if not ds or not ts or ds == "N/A" or ts == "N/A":
        return None
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M:%S.%f"):
        try:
            naive = datetime.strptime(f"{ds} {ts}", fmt)
            return naive.replace(tzinfo=market_tz(code))
        except ValueError:
            continue
    return None


def parse_update_dt(update_time: str, code: str) -> datetime | None:
    s = (update_time or "").strip()
    if not s or s == "N/A":
        return None
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M:%S.%f"):
        try:
            naive = datetime.strptime(s, fmt)
            # snapshot update_time 对港股多为北京时间、美股多为美东（OpenD 习惯）
            return naive.replace(tzinfo=market_tz(code))
        except ValueError:
            continue
    return None


def pct(vals: list[float], p: float) -> float:
    if not vals:
        return float("nan")
    s = sorted(vals)
    if len(s) == 1:
        return s[0]
    k = (len(s) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(s) - 1)
    return s[f] + (s[c] - s[f]) * (k - f)


def summarize(vals: list[float]) -> dict[str, float | int]:
    if not vals:
        return {"n": 0, "p50": 0.0, "p90": 0.0, "mean": 0.0, "min": 0.0, "max": 0.0}
    return {
        "n": len(vals),
        "p50": round(pct(vals, 50), 3),
        "p90": round(pct(vals, 90), 3),
        "mean": round(statistics.fmean(vals), 3),
        "min": round(min(vals), 3),
        "max": round(max(vals), 3),
    }


class QuoteCollector(ft.StockQuoteHandlerBase):
    def __init__(self) -> None:
        super().__init__()
        self.lock = threading.Lock()
        self.events: list[dict[str, Any]] = []
        self.by_code: dict[str, int] = {}
        self.cv = threading.Condition(self.lock)

    def on_recv_rsp(self, rsp_pb):  # type: ignore[no-untyped-def]
        ret, data = super().on_recv_rsp(rsp_pb)
        recv_wall = datetime.now(BJ)
        recv_epoch = time.time()
        if ret != ft.RET_OK:
            return ret, data
        try:
            rows = data.to_dict("records")
        except Exception:
            return ret, data
        with self.cv:
            for row in rows:
                code = str(row.get("code") or "")
                data_dt = parse_quote_dt(str(row.get("data_date") or ""), str(row.get("data_time") or ""), code)
                ms_recv_minus_data = None
                if data_dt is not None:
                    ms_recv_minus_data = (recv_wall.astimezone(data_dt.tzinfo) - data_dt).total_seconds() * 1000.0
                ev = {
                    "code": code,
                    "local_recv_bj": recv_wall.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3],
                    "local_recv_epoch": recv_epoch,
                    "data_date": str(row.get("data_date") or ""),
                    "data_time": str(row.get("data_time") or ""),
                    "data_ts": data_dt.isoformat() if data_dt else "",
                    "last_price": row.get("last_price"),
                    "ms_recv_minus_data": None if ms_recv_minus_data is None else round(ms_recv_minus_data, 3),
                    # BasicQot push 的 data_* 即 pb.updateTime；更新时间与数据时间同源
                    "push_update_same_as_data": True,
                    "ms_recv_minus_push_update": None if ms_recv_minus_data is None else round(ms_recv_minus_data, 3),
                    "ms_push_update_minus_data": 0.0,
                }
                self.events.append(ev)
                self.by_code[code] = self.by_code.get(code, 0) + 1
            self.cv.notify_all()
        return ret, data


def load_symbols(path: Path, limit: int | None) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    symbols = list(raw.get("symbols") or [])
    if limit is not None and limit > 0:
        return symbols[:limit]
    return symbols


def attach_snapshot_update(
    ctx: ft.OpenQuoteContext,
    events: list[dict[str, Any]],
) -> None:
    codes = sorted({e["code"] for e in events if e.get("code")})
    if not codes:
        return
    # batch snapshot
    snap_map: dict[str, str] = {}
    for i in range(0, len(codes), 200):
        chunk = codes[i : i + 200]
        ret, data = ctx.get_market_snapshot(chunk)
        if ret != ft.RET_OK:
            continue
        for _, row in data.iterrows():
            snap_map[str(row["code"])] = str(row.get("update_time") or "")
    for ev in events:
        code = ev["code"]
        upd = snap_map.get(code, "")
        ev["snapshot_update_time"] = upd
        upd_dt = parse_update_dt(upd, code)
        ev["snapshot_update_ts"] = upd_dt.isoformat() if upd_dt else ""
        recv = datetime.strptime(ev["local_recv_bj"], "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=BJ)
        if upd_dt is not None:
            ev["ms_recv_minus_snapshot_update"] = round(
                (recv.astimezone(upd_dt.tzinfo) - upd_dt).total_seconds() * 1000.0, 3
            )
        else:
            ev["ms_recv_minus_snapshot_update"] = None
        data_dt = parse_quote_dt(ev.get("data_date", ""), ev.get("data_time", ""), code)
        if upd_dt is not None and data_dt is not None:
            ev["ms_snapshot_update_minus_data"] = round(
                (upd_dt.astimezone(data_dt.tzinfo) - data_dt).total_seconds() * 1000.0, 3
            )
        else:
            ev["ms_snapshot_update_minus_data"] = None


def run(args: argparse.Namespace) -> int:
    symbols = load_symbols(Path(args.symbols), args.limit)
    codes = [s["code"] for s in symbols]
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    batch_id = datetime.now(BJ).strftime("%Y%m%d_%H%M%S")
    raw_path = out_dir / f"recv_events_{batch_id}.jsonl"
    summary_path = out_dir / f"recv_summary_{batch_id}.json"
    report_path = out_dir / f"行情收到延时_{batch_id}.md"

    collector = QuoteCollector()
    ctx = ft.OpenQuoteContext(host=args.host, port=args.port)
    try:
        ctx.set_handler(collector)
        # subscribe in chunks
        for i in range(0, len(codes), 50):
            chunk = codes[i : i + 50]
            ret, data = ctx.subscribe(chunk, [ft.SubType.QUOTE], subscribe_push=True)
            print(f"subscribe {i+1}-{i+len(chunk)}/{len(codes)} ret={ret} {data}", flush=True)
            if ret != ft.RET_OK:
                print("subscribe failed", data, flush=True)
                return 2

        deadline = time.time() + args.duration_sec
        target = args.min_pushes
        print(f"collecting pushes duration={args.duration_sec}s min_pushes={target}", flush=True)
        while time.time() < deadline:
            with collector.cv:
                if len(collector.events) >= target:
                    break
                collector.cv.wait(timeout=1.0)
            n = len(collector.events)
            if int(time.time()) % 5 == 0:
                print(f"  pushes={n} symbols_seen={len(collector.by_code)}", flush=True)

        events = list(collector.events)
        print(f"collected {len(events)} pushes; attaching snapshots...", flush=True)
        attach_snapshot_update(ctx, events)
    finally:
        try:
            ctx.close()
        except Exception:
            pass

    with raw_path.open("w", encoding="utf-8") as f:
        for ev in events:
            f.write(json.dumps(ev, ensure_ascii=False) + "\n")

    def collect_metric(key: str) -> list[float]:
        out: list[float] = []
        for ev in events:
            v = ev.get(key)
            if v is None:
                continue
            try:
                out.append(float(v))
            except (TypeError, ValueError):
                continue
        return out

    by_market: dict[str, list[dict[str, Any]]] = {"HK": [], "US": [], "ALL": events}
    for ev in events:
        m = "US" if str(ev.get("code", "")).startswith("US.") else "HK"
        by_market[m].append(ev)

    summary: dict[str, Any] = {
        "batch_id": batch_id,
        "host": args.host,
        "port": args.port,
        "duration_sec": args.duration_sec,
        "symbols": len(codes),
        "pushes": len(events),
        "note": (
            "BasicQot push 的 data_date/data_time 来自 pb.updateTime（数据时间=推送更新时间同源）；"
            "snapshot_update_time 来自 get_market_snapshot.update_time。"
        ),
        "metrics": {},
    }
    for mkt, rows in by_market.items():
        if mkt != "ALL" and not rows:
            continue
        subset = rows if mkt != "ALL" else events
        summary["metrics"][mkt] = {
            "pushes": len(subset),
            "ms_recv_minus_data": summarize(
                [float(e["ms_recv_minus_data"]) for e in subset if e.get("ms_recv_minus_data") is not None]
            ),
            "ms_recv_minus_push_update": summarize(
                [
                    float(e["ms_recv_minus_push_update"])
                    for e in subset
                    if e.get("ms_recv_minus_push_update") is not None
                ]
            ),
            "ms_recv_minus_snapshot_update": summarize(
                [
                    float(e["ms_recv_minus_snapshot_update"])
                    for e in subset
                    if e.get("ms_recv_minus_snapshot_update") is not None
                ]
            ),
            "ms_snapshot_update_minus_data": summarize(
                [
                    float(e["ms_snapshot_update_minus_data"])
                    for e in subset
                    if e.get("ms_snapshot_update_minus_data") is not None
                ]
            ),
        }

    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = [
        f"# 行情收到延时 `{batch_id}`",
        "",
        f"- OpenD `{args.host}:{args.port}`；订阅 BasicQot；收集 {args.duration_sec}s / 至少 {args.min_pushes} 条 push",
        f"- 标的数 {len(codes)}；收到 push {len(events)}",
        "- **数据时间** = push.`data_date`+`data_time`（API 来自 `pb.updateTime`）",
        "- **推送更新时间** = 与数据时间同源（BasicQot 无第二套时间戳）",
        "- **快照更新时间** = `get_market_snapshot.update_time`",
        "- **本地收到** = 本机收到 push 的北京时间",
        "",
        "## 三者差（ms）",
        "",
        "| 市场 | 指标 | n | 中位 | p90 | 均值 | 最小 | 最大 |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    label = {
        "ms_recv_minus_data": "本地收到 − 数据时间",
        "ms_recv_minus_push_update": "本地收到 − 推送更新时间",
        "ms_recv_minus_snapshot_update": "本地收到 − 快照更新时间",
        "ms_snapshot_update_minus_data": "快照更新时间 − 数据时间",
    }
    for mkt in ("ALL", "HK", "US"):
        if mkt not in summary["metrics"]:
            continue
        for key, name in label.items():
            st = summary["metrics"][mkt][key]
            lines.append(
                f"| {mkt} | {name} | {st['n']} | {st['p50']} | {st['p90']} | {st['mean']} | {st['min']} | {st['max']} |"
            )
    lines += [
        "",
        "## 说明",
        "",
        "1. 休市标的的「数据时间」停在最后成交附近，本地收到 − 数据时间会到小时级，属预期。",
        "2. 盘中活跃标的应以「本地收到 − 数据/推送更新」的中位为主要延时口径。",
        "3. 原始事件：" + str(raw_path),
        "",
    ]
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {raw_path}", flush=True)
    print(f"wrote {summary_path}", flush=True)
    print(f"wrote {report_path}", flush=True)
    # print ALL summary briefly
    for key, name in label.items():
        st = summary["metrics"]["ALL"][key]
        print(f"ALL {name}: n={st['n']} p50={st['p50']} mean={st['mean']} min={st['min']} max={st['max']}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Market data recv vs data/update time latency")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11121)
    ap.add_argument(
        "--symbols",
        default=str(ROOT / "bench/config/market_data_symbols_50.json"),
    )
    ap.add_argument("--limit", type=int, default=0, help="0=all symbols")
    ap.add_argument("--duration-sec", type=float, default=45.0)
    ap.add_argument("--min-pushes", type=int, default=100)
    ap.add_argument(
        "--out-dir",
        default=str(ROOT / "reports" / "market_data_recv_latency"),
    )
    args = ap.parse_args()
    if args.limit <= 0:
        args.limit = None
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())

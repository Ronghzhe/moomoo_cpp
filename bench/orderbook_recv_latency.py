#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OrderBook 推送延时：数据时间 / 推送到达 / 本地时间 三者差。

通道：OpenD SDK `SubType.ORDER_BOOK`（TCP 推送，非浏览器 WebSocket）。

时间口径：
  - 数据时间 = pb.svrRecvTimeBid/Ask（+ Timestamp）：富途服务器从交易所收到盘口的时间
  - 推送时间 = 回调入口时刻（OpenD 无「服务器发出推送」字段；推送到达≈本地收包）
  - 本地时间 = 解包完成后的本机 wall clock（与推送时间之差≈本进程处理开销）

有意义的主指标：本地时间 − 数据时间（bid/ask）。
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

import moomoo as ft  # noqa: E402

BJ = ZoneInfo("Asia/Shanghai")


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


def load_symbols(path: Path, limit: int | None) -> list[str]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    codes = [str(s["code"]) for s in (raw.get("symbols") or []) if s.get("code")]
    if limit is not None and limit > 0:
        return codes[:limit]
    return codes


def fmt_bj(epoch: float) -> str:
    return datetime.fromtimestamp(epoch, BJ).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


class OrderBookCollector(ft.OrderBookHandlerBase):
    def __init__(self) -> None:
        super().__init__()
        self.lock = threading.Lock()
        self.cv = threading.Condition(self.lock)
        self.events: list[dict[str, Any]] = []
        self.by_code: dict[str, int] = {}
        self.first_push_epoch: float | None = None

    def on_recv_rsp(self, rsp_pb):  # type: ignore[no-untyped-def]
        # 推送时间：回调入口（推送到达本进程）
        push_epoch = time.time()
        ret, data = super().on_recv_rsp(rsp_pb)
        # 本地时间：解包后
        local_epoch = time.time()
        if ret != ft.RET_OK or not isinstance(data, dict):
            return ret, data

        s2c = rsp_pb.s2c
        code = str(data.get("code") or "")
        bid_str = str(getattr(s2c, "svrRecvTimeBid", "") or data.get("svr_recv_time_bid") or "")
        ask_str = str(getattr(s2c, "svrRecvTimeAsk", "") or data.get("svr_recv_time_ask") or "")
        bid_ts = float(getattr(s2c, "svrRecvTimeBidTimestamp", 0.0) or 0.0)
        ask_ts = float(getattr(s2c, "svrRecvTimeAskTimestamp", 0.0) or 0.0)
        has_bid_ts = bool(s2c.HasField("svrRecvTimeBidTimestamp") and bid_ts > 0)
        has_ask_ts = bool(s2c.HasField("svrRecvTimeAskTimestamp") and ask_ts > 0)

        ms_local_minus_push = (local_epoch - push_epoch) * 1000.0
        ms_local_minus_data_bid = (local_epoch - bid_ts) * 1000.0 if has_bid_ts else None
        ms_local_minus_data_ask = (local_epoch - ask_ts) * 1000.0 if has_ask_ts else None
        ms_push_minus_data_bid = (push_epoch - bid_ts) * 1000.0 if has_bid_ts else None
        ms_push_minus_data_ask = (push_epoch - ask_ts) * 1000.0 if has_ask_ts else None
        ms_ask_minus_bid = None
        if has_bid_ts and has_ask_ts:
            ms_ask_minus_bid = (ask_ts - bid_ts) * 1000.0

        # 综合数据时间：取 bid/ask 中较新的一侧
        data_epoch = None
        if has_bid_ts and has_ask_ts:
            data_epoch = max(bid_ts, ask_ts)
        elif has_bid_ts:
            data_epoch = bid_ts
        elif has_ask_ts:
            data_epoch = ask_ts
        ms_local_minus_data = (local_epoch - data_epoch) * 1000.0 if data_epoch is not None else None
        ms_push_minus_data = (push_epoch - data_epoch) * 1000.0 if data_epoch is not None else None

        ev = {
            "code": code,
            "market": "US" if code.startswith("US.") else "HK",
            "n_bid": len(getattr(s2c, "orderBookBidList", []) or []),
            "n_ask": len(getattr(s2c, "orderBookAskList", []) or []),
            # 三者
            "data_bid_str": bid_str,
            "data_ask_str": ask_str,
            "data_bid_epoch": bid_ts if has_bid_ts else None,
            "data_ask_epoch": ask_ts if has_ask_ts else None,
            "data_epoch": data_epoch,
            "push_epoch": push_epoch,
            "push_bj": fmt_bj(push_epoch),
            "local_epoch": local_epoch,
            "local_bj": fmt_bj(local_epoch),
            # 差值 ms
            "ms_local_minus_data": None if ms_local_minus_data is None else round(ms_local_minus_data, 3),
            "ms_push_minus_data": None if ms_push_minus_data is None else round(ms_push_minus_data, 3),
            "ms_local_minus_push": round(ms_local_minus_push, 3),
            "ms_local_minus_data_bid": None
            if ms_local_minus_data_bid is None
            else round(ms_local_minus_data_bid, 3),
            "ms_local_minus_data_ask": None
            if ms_local_minus_data_ask is None
            else round(ms_local_minus_data_ask, 3),
            "ms_push_minus_data_bid": None
            if ms_push_minus_data_bid is None
            else round(ms_push_minus_data_bid, 3),
            "ms_push_minus_data_ask": None
            if ms_push_minus_data_ask is None
            else round(ms_push_minus_data_ask, 3),
            "ms_ask_minus_bid": None if ms_ask_minus_bid is None else round(ms_ask_minus_bid, 3),
            "has_data_time": data_epoch is not None,
        }
        with self.cv:
            if self.first_push_epoch is None:
                self.first_push_epoch = push_epoch
            self.events.append(ev)
            self.by_code[code] = self.by_code.get(code, 0) + 1
            self.cv.notify_all()
        return ret, data


def run(args: argparse.Namespace) -> int:
    codes = load_symbols(Path(args.symbols), args.limit)
    if args.codes:
        codes = [c.strip() for c in args.codes.split(",") if c.strip()]
    if not codes:
        print("no symbols", flush=True)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    batch_id = datetime.now(BJ).strftime("%Y%m%d_%H%M%S")
    raw_path = out_dir / f"orderbook_events_{batch_id}.jsonl"
    summary_path = out_dir / f"orderbook_summary_{batch_id}.json"
    report_path = out_dir / f"OrderBook延时_{batch_id}.md"

    collector = OrderBookCollector()
    t_conn0 = time.time()
    ctx = ft.OpenQuoteContext(host=args.host, port=args.port)
    connect_ms = (time.time() - t_conn0) * 1000.0
    try:
        ctx.set_handler(collector)
        t_sub0 = time.time()
        for i in range(0, len(codes), 50):
            chunk = codes[i : i + 50]
            ret, err = ctx.subscribe(
                chunk,
                [ft.SubType.ORDER_BOOK],
                subscribe_push=True,
                is_first_push=True,
            )
            print(f"subscribe {i + 1}-{i + len(chunk)}/{len(codes)} ret={ret} {err}", flush=True)
            if ret != ft.RET_OK:
                print("subscribe failed", err, flush=True)
                return 2
        subscribe_ms = (time.time() - t_sub0) * 1000.0
        sub_done_epoch = time.time()

        deadline = time.time() + args.duration_sec
        print(
            f"collecting OrderBook pushes duration={args.duration_sec}s "
            f"min_pushes={args.min_pushes} symbols={len(codes)}",
            flush=True,
        )
        while time.time() < deadline:
            with collector.cv:
                if len(collector.events) >= args.min_pushes:
                    break
                collector.cv.wait(timeout=1.0)
            if int(time.time()) % 5 == 0:
                print(
                    f"  pushes={len(collector.events)} symbols_seen={len(collector.by_code)}",
                    flush=True,
                )

        events = list(collector.events)
        first_push_ms = None
        if collector.first_push_epoch is not None:
            first_push_ms = (collector.first_push_epoch - sub_done_epoch) * 1000.0
    finally:
        try:
            ctx.close()
        except Exception:
            pass

    with raw_path.open("w", encoding="utf-8") as f:
        for ev in events:
            f.write(json.dumps(ev, ensure_ascii=False) + "\n")

    def metric_vals(rows: list[dict[str, Any]], key: str) -> list[float]:
        out: list[float] = []
        for e in rows:
            v = e.get(key)
            if v is None:
                continue
            out.append(float(v))
        return out

    by_market: dict[str, list[dict[str, Any]]] = {
        "ALL": events,
        "US": [e for e in events if e.get("market") == "US"],
        "HK": [e for e in events if e.get("market") == "HK"],
    }

    metric_keys = [
        "ms_local_minus_data",
        "ms_push_minus_data",
        "ms_local_minus_push",
        "ms_local_minus_data_bid",
        "ms_local_minus_data_ask",
        "ms_ask_minus_bid",
    ]

    summary: dict[str, Any] = {
        "batch_id": batch_id,
        "host": args.host,
        "port": args.port,
        "duration_sec": args.duration_sec,
        "symbols": len(codes),
        "codes": codes,
        "pushes": len(events),
        "pushes_with_data_time": sum(1 for e in events if e.get("has_data_time")),
        "connect_ms": round(connect_ms, 3),
        "subscribe_ms": round(subscribe_ms, 3),
        "first_push_ms": None if first_push_ms is None else round(first_push_ms, 3),
        "note": (
            "数据时间=svrRecvTime*Timestamp；推送时间=回调入口；本地时间=解包后；"
            "OpenD 无独立服务器发出推送时刻，故推送≈本地收包，主看 本地−数据。"
        ),
        "metrics": {},
    }
    for mkt, rows in by_market.items():
        if mkt != "ALL" and not rows:
            continue
        summary["metrics"][mkt] = {
            "pushes": len(rows),
            "with_data_time": sum(1 for e in rows if e.get("has_data_time")),
        }
        for k in metric_keys:
            summary["metrics"][mkt][k] = summarize(metric_vals(rows, k))

    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    label = {
        "ms_local_minus_data": "本地时间 − 数据时间",
        "ms_push_minus_data": "推送时间 − 数据时间",
        "ms_local_minus_push": "本地时间 − 推送时间",
        "ms_local_minus_data_bid": "本地时间 − 买盘数据时间",
        "ms_local_minus_data_ask": "本地时间 − 卖盘数据时间",
        "ms_ask_minus_bid": "卖盘数据时间 − 买盘数据时间",
    }
    lines = [
        f"# OrderBook 延时 `{batch_id}`",
        "",
        f"- OpenD `{args.host}:{args.port}`；订阅 `ORDER_BOOK`",
        f"- 标的 {len(codes)}；收到 push {len(events)}（含数据时间 {summary['pushes_with_data_time']}）",
        f"- 连接 `connect_ms`={summary['connect_ms']}；订阅 `subscribe_ms`={summary['subscribe_ms']}；"
        f"订阅完成→首推 `first_push_ms`={summary['first_push_ms']}",
        "",
        "## 时间口径",
        "",
        "| 名称 | 来源 |",
        "| --- | --- |",
        "| **数据时间** | `svrRecvTimeBid/Ask` + `*Timestamp`（富途收交易所盘口时间） |",
        "| **推送时间** | `on_recv_rsp` 回调入口（OpenD 无服务器发出时刻字段） |",
        "| **本地时间** | 解包完成后的本机 wall clock |",
        "",
        "## 三者差（ms）",
        "",
        "| 市场 | 指标 | n | 中位 | p90 | 均值 | 最小 | 最大 |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for mkt in ("ALL", "US", "HK"):
        if mkt not in summary["metrics"]:
            continue
        for key, name in label.items():
            st = summary["metrics"][mkt][key]
            lines.append(
                f"| {mkt} | {name} | {st['n']} | {st['p50']} | {st['p90']} | "
                f"{st['mean']} | {st['min']} | {st['max']} |"
            )
    lines += [
        "",
        "## 说明",
        "",
        "1. 主看 **本地时间 − 数据时间**（及推送时间 − 数据时间；二者几乎相同）。",
        "2. **本地 − 推送** 仅为回调内解包开销，通常亚毫秒～数毫秒。",
        "3. 休市/缓存首推时 `svrRecvTime*` 可能为空，不计入差值统计。",
        "4. 原始事件：" + str(raw_path),
        "",
    ]
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"wrote {raw_path}", flush=True)
    print(f"wrote {summary_path}", flush=True)
    print(f"wrote {report_path}", flush=True)
    print(
        f"connect_ms={summary['connect_ms']} subscribe_ms={summary['subscribe_ms']} "
        f"first_push_ms={summary['first_push_ms']}",
        flush=True,
    )
    for key, name in label.items():
        st = summary["metrics"]["ALL"][key]
        print(
            f"ALL {name}: n={st['n']} p50={st['p50']} mean={st['mean']} "
            f"min={st['min']} max={st['max']}",
            flush=True,
        )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="OrderBook recv vs data/push/local latency")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11121)
    ap.add_argument(
        "--symbols",
        default=str(ROOT / "bench/config/market_data_symbols_50.json"),
    )
    ap.add_argument("--codes", default="", help="comma codes overrides --symbols, e.g. US.AAPL,HK.00700")
    ap.add_argument("--limit", type=int, default=0, help="0=all from symbols file")
    ap.add_argument("--duration-sec", type=float, default=30.0)
    ap.add_argument("--min-pushes", type=int, default=50)
    ap.add_argument(
        "--out-dir",
        default=str(ROOT / "reports" / "orderbook_recv_latency"),
    )
    args = ap.parse_args()
    if args.limit <= 0:
        args.limit = None
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())

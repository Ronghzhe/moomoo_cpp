#!/usr/bin/env python3
"""Test 4 — Python 侧调用延时：公式/条件求值、Telegram 文本/图片发送。"""

from __future__ import annotations

import argparse
import csv
import statistics
import struct
import sys
import time
import zlib
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]
BENCH_DIR = Path(__file__).resolve().parent
MOOMOO_PY = Path("/home/ubuntu/workspace/moomoo_python")
CSV_PATH = ROOT / "strategy/task_system/bench_logs/python_call_latency.csv"
REPORT_DIR = ROOT / "reports"
TG_ASSET_DIR = ROOT / "strategy/task_system/bench_logs/tg_assets"

for p in (str(MOOMOO_PY), str(MOOMOO_PY / "strategy"), str(BENCH_DIR.parent / "python")):
    if p not in sys.path:
        sys.path.insert(0, p)

from bench_common import percentile, setup_python_path  # noqa: E402

setup_python_path()

from condition_engine.context import ExitEvalContext  # noqa: E402
from condition_engine.evaluators.shared import orderbook as ob_eval  # noqa: E402
from strategy_types import PriceCondition, VolumeEntryWhen  # noqa: E402
from strategy_main import volume_spike_eval_from_volumes  # noqa: E402
from telegram.config import load_config as load_tg_config  # noqa: E402
from telegram.notify import send_telegram_photo, send_telegram_text  # noqa: E402


def _ms_since(t0: float) -> float:
    return (time.perf_counter() - t0) * 1000.0


def _bench(name: str, op: str, fn: Callable[[], Any], *, iters: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for i in range(1, iters + 1):
        t0 = time.perf_counter()
        ok = True
        err = ""
        try:
            fn()
        except Exception as e:
            ok = False
            err = str(e)
        rows.append(
            {
                "module": name,
                "operation": op,
                "iter": i,
                "latency_ms": round(_ms_since(t0), 4),
                "ok": ok,
                "error": err,
            }
        )
    return rows


def _summarize(rows: list[dict[str, Any]]) -> dict[str, float | int]:
    vals = [float(r["latency_ms"]) for r in rows if r.get("ok")]
    if not vals:
        return {"n": 0, "p50": 0.0, "p99": 0.0, "mean": 0.0, "stdev": 0.0, "min": 0.0, "max": 0.0}
    return {
        "n": len(vals),
        "p50": round(percentile(vals, 50), 4),
        "p99": round(percentile(vals, 99), 4),
        "mean": round(statistics.fmean(vals), 4),
        "stdev": round(statistics.pstdev(vals), 4) if len(vals) > 1 else 0.0,
        "min": round(min(vals), 4),
        "max": round(max(vals), 4),
    }


def bench_formula(*, iters: int) -> list[dict[str, Any]]:
    cond = PriceCondition(field="mid", op=">=", value=100.0)
    ctx = ExitEvalContext(fields={"mid": 105.5}, gain_fraction=0.02)
    rows = _bench("condition_engine", "orderbook_eval", lambda: ob_eval.evaluate(cond, ctx), iters=iters)

    vols = [float(x) for x in range(100, 130)]
    vw = VolumeEntryWhen(kl_type="K_5M", ma_period=20, volume_multiple=2.0)
    rows += _bench(
        "strategy_main",
        "volume_spike_eval",
        lambda: volume_spike_eval_from_volumes(vols, vw),
        iters=iters,
    )
    return rows


def _resolve_chat(target: str) -> tuple[str, str, str]:
    cfg = load_tg_config()
    target = (target or "paper").strip().lower()
    if target == "monitor":
        ep = cfg.monitor
    elif target == "real":
        ep = cfg.real
    else:
        ep = cfg.paper
    chat_id = (ep.chat_id or "").strip()
    title = (ep.chat_title or target).strip()
    if not chat_id:
        chat_id = (cfg.paper.chat_id or cfg.monitor.chat_id or "").strip()
        title = (cfg.paper.chat_title or cfg.monitor.chat_title or "fallback").strip()
    return (cfg.bot_token or "").strip(), chat_id, title


def _tiny_png(path: Path, *, w: int = 64, h: int = 32) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = b""
    for _y in range(h):
        raw += b"\x00"
        for _x in range(w):
            raw += b"\x2a\x82\xe4\xff"
    compressed = zlib.compress(raw, 9)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
    path.write_bytes(png)
    return path


def bench_telegram(
    *,
    iters: int,
    skip_send: bool,
    target: str = "paper",
    include_photo: bool = True,
    interval_sec: float = 0.35,
) -> list[dict[str, Any]]:
    if skip_send:
        return [
            {
                "module": "telegram",
                "operation": "send_text",
                "iter": 0,
                "latency_ms": 0.0,
                "ok": False,
                "error": "skipped_by_flag",
            }
        ]

    token, chat_id, title = _resolve_chat(target)
    if not token or not chat_id:
        return [
            {
                "module": "telegram",
                "operation": "send_text",
                "iter": 0,
                "latency_ms": 0.0,
                "ok": False,
                "error": "missing_bot_token_or_chat_id",
            }
        ]

    batch_tag = datetime.now().strftime("%Y%m%d_%H%M%S")
    print(
        f"[tg] target={target} chat={title!r} id={chat_id} token_len={len(token)} iters={iters}",
        flush=True,
    )

    rows: list[dict[str, Any]] = []
    for i in range(1, iters + 1):
        text = f"[PERF Test4] send_text batch={batch_tag} iter={i}/{iters} target={target}"
        t0 = time.perf_counter()
        ok = True
        err = ""
        try:
            sent_ok, sent_err = send_telegram_text(bot_token=token, chat_id=chat_id, text=text)
            if not sent_ok:
                raise RuntimeError(sent_err or "send_text_failed")
        except Exception as e:
            ok = False
            err = str(e)
        rows.append(
            {
                "module": "telegram",
                "operation": "send_text",
                "iter": i,
                "latency_ms": round(_ms_since(t0), 4),
                "ok": ok,
                "error": err,
            }
        )
        if i < iters:
            time.sleep(interval_sec)

    if include_photo:
        png = _tiny_png(TG_ASSET_DIR / f"perf_test4_{batch_tag}.png")
        photo_n = max(1, min(3, iters))
        for i in range(1, photo_n + 1):
            cap = f"[PERF Test4] send_photo batch={batch_tag} iter={i} target={target}"
            t0 = time.perf_counter()
            ok = True
            err = ""
            try:
                sent_ok, sent_err = send_telegram_photo(
                    bot_token=token,
                    chat_id=chat_id,
                    photo_path=png,
                    caption=cap,
                )
                if not sent_ok:
                    raise RuntimeError(sent_err or "send_photo_failed")
            except Exception as e:
                ok = False
                err = str(e)
            rows.append(
                {
                    "module": "telegram",
                    "operation": "send_photo",
                    "iter": i,
                    "latency_ms": round(_ms_since(t0), 4),
                    "ok": ok,
                    "error": err,
                }
            )
            if i < photo_n:
                time.sleep(interval_sec)
    return rows


def append_csv(batch_id: str, rows: list[dict[str, Any]]) -> None:
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    write_header = not CSV_PATH.is_file() or CSV_PATH.stat().st_size == 0
    with CSV_PATH.open("a", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=["batch_id", "ts", "module", "operation", "iter", "latency_ms", "ok", "error"],
        )
        if write_header:
            w.writeheader()
        ts = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        for r in rows:
            w.writerow({"batch_id": batch_id, "ts": ts, **r})


def write_report(batch_id: str, groups: dict[tuple[str, str], list[dict[str, Any]]]) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = REPORT_DIR / f"python_call_latency_{batch_id}.md"
    lines = [
        f"# Test 4 — Python 调用延时 ({batch_id})",
        "",
        f"**时间：** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} (UTC+8)",
        f"**CSV：** `{CSV_PATH}`",
        "",
        "## 统计",
        "",
        "| module | operation | n | p50_ms | p99_ms | mean_ms | stdev_ms | min_ms | max_ms | ok |",
        "|--------|-----------|--:|-------:|-------:|--------:|---------:|-------:|-------:|----|",
    ]
    for (mod, op), rows in sorted(groups.items()):
        ok_n = sum(1 for r in rows if r.get("ok"))
        s = _summarize(rows)
        lines.append(
            f"| {mod} | {op} | {s['n']} | {s['p50']} | {s['p99']} | {s['mean']} | {s['stdev']} | "
            f"{s['min']} | {s['max']} | {ok_n}/{len(rows)} |"
        )
    lines += [
        "",
        "## 通过标准（草案）",
        "",
        "- P1: TG send_text p50 < 2000ms",
        "- P1b: TG send_photo p50 < 3000ms（网络正常）",
        "- P2: orderbook_eval p50 < 10ms",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def main() -> int:
    ap = argparse.ArgumentParser(description="Test 4: Python call latency bench")
    ap.add_argument("--formula-iters", type=int, default=5000)
    ap.add_argument("--tg-iters", type=int, default=5)
    ap.add_argument("--skip-tg", action="store_true", help="skip real Telegram send")
    ap.add_argument("--tg-only", action="store_true", help="only run Telegram benches")
    ap.add_argument("--formula-only", action="store_true", help="only run formula benches")
    ap.add_argument("--tg-target", choices=("paper", "monitor", "real"), default="paper")
    ap.add_argument("--no-photo", action="store_true", help="skip send_photo")
    ap.add_argument("--check-config", action="store_true", help="print TG config and exit")
    args = ap.parse_args()

    if args.check_config:
        token, chat_id, title = _resolve_chat(args.tg_target)
        print(f"tg_target={args.tg_target}")
        print(f"token_set={bool(token)} token_len={len(token)}")
        print(f"chat_id={chat_id}")
        print(f"chat_title={title}")
        return 0 if token and chat_id else 2

    batch_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    all_rows: list[dict[str, Any]] = []
    if not args.tg_only:
        all_rows += bench_formula(iters=args.formula_iters)
    if not args.formula_only:
        all_rows += bench_telegram(
            iters=args.tg_iters,
            skip_send=args.skip_tg,
            target=args.tg_target,
            include_photo=not args.no_photo,
        )

    append_csv(batch_id, all_rows)
    groups: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for r in all_rows:
        key = (str(r["module"]), str(r["operation"]))
        groups.setdefault(key, []).append(r)

    report = write_report(batch_id, groups)
    print(f"batch_id={batch_id} rows={len(all_rows)} report={report}")
    for (mod, op), rows in sorted(groups.items()):
        s = _summarize(rows)
        ok_n = sum(1 for r in rows if r.get("ok"))
        print(f"  {mod}.{op}: ok={ok_n}/{len(rows)} p50={s['p50']}ms mean={s['mean']}ms")

    scored = [r for r in all_rows if int(r.get("iter") or 0) > 0]
    return 0 if scored and all(r.get("ok") for r in scored) else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gateway HTTP place + Python 直连 cancel（模拟盘，与 executor -gateway 一致）。"""

from __future__ import annotations

import argparse
import json
import sys
import time
import uuid
from pathlib import Path
from urllib import error as urlerror
from urllib import request as urlrequest

from bench_common import append_jsonl, load_config, ms_since, setup_python_path  # noqa: E402

setup_python_path()

from order_client import OrderClient, OrderConfig, extract_order_id  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def _gateway_health(base: str, timeout: float = 3.0) -> bool:
    req = urlrequest.Request(f"{base.rstrip('/')}/health", method="GET")
    try:
        with urlrequest.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            data = json.loads(body) if body.strip().startswith("{") else {}
            return bool(data.get("ok", True))
    except Exception:
        return False


def _gateway_place(base: str, cfg: dict, timeout: float = 15.0) -> tuple[bool, str, float, dict]:
    code = str(cfg.get("code") or "US.AAPL")
    payload = {
        "request_id": str(uuid.uuid4()),
        "code": code,
        "side": str(cfg.get("order_side") or "BUY"),
        "qty": float(cfg.get("order_qty") or 1),
        "price": float(cfg.get("order_price") or 0.01),
        "order_type": str(cfg.get("order_type") or "NORMAL"),
        "remark": "bench_gateway",
        "context": {
            "strategy_id": "bench",
            "symbol": code,
            "trd_env": "SIMULATE",
            "trd_market": "US",
            "opend_host": str(cfg.get("opend_host") or "127.0.0.1"),
            "opend_port": int(cfg.get("opend_port") or 11121),
            "priority": 50,
        },
    }
    raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urlrequest.Request(
        f"{base.rstrip('/')}/v1/orders/place",
        data=raw,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    t0 = time.perf_counter()
    try:
        with urlrequest.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            ms = ms_since(t0)
            data = json.loads(body) if body.strip() else {}
            if not data.get("ok"):
                return False, str(data.get("error") or body), ms, data
            order_id = str(data.get("order_id") or "")
            return bool(order_id), order_id, ms, data
    except urlerror.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        return False, f"http_{e.code}:{body}", ms_since(t0), {}
    except urlerror.URLError as e:
        return False, f"gateway_unreachable:{e}", ms_since(t0), {}


def main() -> int:
    ap = argparse.ArgumentParser(description="Gateway place + direct cancel benchmark (SIMULATE)")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench.yaml"))
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    cfg = load_config(Path(args.config))
    if str(cfg.get("trd_env") or "SIMULATE").upper() != "SIMULATE":
        print("ERROR: trd_env must be SIMULATE", file=sys.stderr)
        return 2

    base = str(cfg.get("gateway_url") or "http://127.0.0.1:18765")
    if not _gateway_health(base):
        print(f"ERROR: gateway not healthy at {base}", file=sys.stderr)
        print("Start: moomoo_python/strategy/task_system/script/gateway.sh start", file=sys.stderr)
        return 2

    out = Path(args.out)
    if out.exists():
        out.unlink()

    iters = int(cfg.get("order_iters") or 30)
    gap = float(cfg.get("order_gap_sec") or 0.05)
    order_cfg = OrderConfig(
        host=str(cfg.get("opend_host") or "127.0.0.1"),
        port=int(cfg.get("opend_port") or 11121),
        trd_env="SIMULATE",
        trd_market="US",
    )
    cancel_client = OrderClient(order_cfg)
    ok = 0
    try:
        cancel_client.unlock()
        for i in range(1, iters + 1):
            t0 = time.perf_counter()
            placed, order_id_or_err, ms_place, gw_data = _gateway_place(base, cfg)
            ms_query = 0.0
            ms_cancel = 0.0
            success = placed
            order_id = order_id_or_err if placed else ""
            if placed:
                time.sleep(max(0.0, gap))
                t_q = time.perf_counter()
                cancel_client.get_order(order_id=order_id, code=str(cfg.get("code") or "US.AAPL"))
                ms_query = ms_since(t_q)
                time.sleep(max(0.0, gap))
                t_c = time.perf_counter()
                ret_c, out_c = cancel_client.cancel_order(order_id)
                ms_cancel = ms_since(t_c)
                success = ret_c == 0
                if not success:
                    print(f"  gateway cancel fail iter={i}: {out_c}", file=sys.stderr)
            append_jsonl(
                out,
                {
                    "case": "order_warm",
                    "lang": "gateway",
                    "iter": i,
                    "ok": success and placed,
                    "order_id": order_id,
                    "ms_total": round(ms_since(t0), 3),
                    "ms_place": round(ms_place, 3),
                    "ms_http": round(ms_place, 3),
                    "ms_query": round(ms_query, 3),
                    "ms_cancel": round(ms_cancel, 3),
                    "gateway_exec_ms": gw_data.get("exec_ms"),
                    "gateway_queue_wait_ms": gw_data.get("queue_wait_ms"),
                    "error": "" if placed else order_id_or_err,
                    "trd_env": "SIMULATE",
                },
            )
            ok += int(success and placed)
    finally:
        cancel_client.close()

    print(f"[python order gateway] ok={ok}/{iters}")
    return 0 if ok == iters else 1


if __name__ == "__main__":
    raise SystemExit(main())

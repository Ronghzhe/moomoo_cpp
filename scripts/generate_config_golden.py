#!/usr/bin/env python3
"""Generate golden summary JSON from Python strategy_loader for C++ parity tests."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY_STRATEGY = ROOT.parent / "moomoo_python" / "strategy"
FIXTURES = ROOT / "tests" / "fixtures"
GOLDEN = FIXTURES / "golden"

sys.path.insert(0, str(PY_STRATEGY))
from strategy_loader import load_config  # noqa: E402


def summarize(path: Path) -> dict:
    cfg = load_config(str(path))
    syms = []
    for s in cfg.symbols:
        es = []
        for r in s.entry_strategies or []:
            es.append(
                {
                    "id": r.rule_id,
                    "enabled": r.enabled,
                    "logic": r.logic,
                    "condition_types": [type(c).__name__ for c in r.conditions],
                }
            )
        xs = []
        for r in s.exit_strategies or []:
            xs.append(
                {
                    "id": r.rule_id,
                    "enabled": r.enabled,
                    "logic": r.logic,
                    "side": r.side,
                    "condition_types": [type(c).__name__ for c in r.conditions],
                }
            )
        syms.append(
            {
                "code": s.code,
                "enabled": s.enabled,
                "entry_side": s.entry_side,
                "order_type": s.order_type,
                "time_in_force": s.time_in_force,
                "entry_qty": s.entry_qty,
                "budget": s.budget,
                "status": s.status,
                "sim_market_pct": s.sim_market_pct,
                "exit_enabled": s.exit.enabled,
                "entry_strategy_logic": s.entry_strategy_logic,
                "exit_strategy_logic": s.exit_strategy_logic,
                "entry_strategies": es,
                "exit_strategies": xs,
                "task_id_nonempty": bool(s.task_id.strip()),
            }
        )
    return {
        "version": cfg.version,
        "client_name": cfg.client_name,
        "strategy_name": cfg.strategy_name,
        "mode": cfg.mode,
        "opend_host": cfg.opend_host,
        "opend_port": cfg.opend_port,
        "trd_market": cfg.trd_market,
        "order_type": cfg.order_type,
        "order_poll_min_sec": cfg.order_poll_min_sec,
        "dingding_enabled": cfg.dingding_enabled,
        "symbol_count": len(cfg.symbols),
        "symbols": syms,
    }


def main() -> int:
    GOLDEN.mkdir(parents=True, exist_ok=True)
    for name in ("us", "hk"):
        src = FIXTURES / f"strategy_config_{name}_fixture.json"
        dst = GOLDEN / f"strategy_config_{name}_fixture.golden.json"
        data = summarize(src)
        dst.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("wrote", dst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

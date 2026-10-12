#!/usr/bin/env python3
"""Generate L2 replay inputs + Python golden outputs for C++ parity replay tests."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "tests" / "replay"
GOLDEN = ROOT / "tests" / "golden" / "l2"
FIXTURES = ROOT / "tests" / "fixtures"
PY_ROOT = ROOT.parent / "moomoo_python"
PY_STRATEGY = PY_ROOT / "strategy"

sys.path.insert(0, str(PY_ROOT))
sys.path.insert(0, str(PY_STRATEGY))

from book_guard_emergency_exit import (  # noqa: E402
    _emergency_limit_submit_px,
    emergency_exit_ref_px,
)
from condition_engine.evaluators.exit import pnl as pnl_eval  # noqa: E402
from condition_engine.evaluators.shared import orderbook as ob_eval  # noqa: E402
from condition_engine.evaluators.exit.staged_take_profit import (  # noqa: E402
    allocate_tranche_qtys,
    resolve_staged_baseline_qty,
)
from hk_short_params import HK_SHORT_ASK_BAND_PCT  # noqa: E402
from strategy_loader import load_config  # noqa: E402
from strategy_types import PnlCondition, PriceCondition  # noqa: E402


def _write_pair(scenario_id: str, replay: dict[str, Any], golden: dict[str, Any]) -> None:
    REPLAY.mkdir(parents=True, exist_ok=True)
    GOLDEN.mkdir(parents=True, exist_ok=True)
    replay_path = REPLAY / f"{scenario_id}.json"
    golden_path = GOLDEN / f"{scenario_id}.golden.json"
    replay_path.write_text(json.dumps(replay, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    golden_path.write_text(json.dumps(golden, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("wrote", replay_path.name, golden_path.name)


def _bool_merge(values: list[bool], logic: str) -> bool:
    if not values:
        return False
    if logic == "all":
        return all(values)
    return any(values)


def _eval_orderbook(field: str, op: str, value: float, fields: dict[str, float]) -> bool:
    cond = PriceCondition(field=field, op=op, value=value)
    res = ob_eval.evaluate(cond, type("Ctx", (), {"fields": fields})())
    return bool(res.hit)


def _eval_pnl(op: str, value: float, gain_fraction: float) -> bool:
    cond = PnlCondition(op=op, value=value)
    ctx = type("Ctx", (), {"gain_fraction": gain_fraction})()
    res = pnl_eval.evaluate(cond, ctx)
    return bool(res.hit)


def _judge_rules(rules: list[Any], logic: str, fields: dict[str, float],
                 gain_fraction: float | None = None) -> tuple[bool, str]:
    hits: list[bool] = []
    hit_id = ""
    for rule in rules:
        if not rule.enabled:
            continue
        cond_hits: list[bool] = []
        for cond in rule.conditions:
            if isinstance(cond, PriceCondition):
                cond_hits.append(
                    _eval_orderbook(cond.field, cond.op or "", float(cond.value or 0), fields)
                )
            elif isinstance(cond, PnlCondition) and gain_fraction is not None:
                cond_hits.append(_eval_pnl(cond.op, float(cond.value), gain_fraction))
        rule_hit = _bool_merge(cond_hits, rule.logic or "all")
        hits.append(rule_hit)
        if rule_hit and not hit_id:
            hit_id = rule.rule_id
    return _bool_merge(hits, logic or "any"), hit_id


def _scenario_strategy_judge_us_orderbook() -> None:
    sid = "l2_01_strategy_judge_us_orderbook"
    cfg = load_config(str(FIXTURES / "strategy_config_us_fixture.json"))
    sym = cfg.symbols[0]
    steps_in = [
        {"label": "entry_hit", "tick": {"bid_1": 13.9, "ask_1": 14.0, "now": 1.0}},
        {"label": "entry_miss", "tick": {"bid_1": 13.9, "ask_1": 16.0, "now": 2.0}},
        {"label": "exit_hit", "tick": {"bid_1": 24.0, "ask_1": 24.1, "now": 3.0}},
        {
            "label": "entry_and_exit",
            "tick": {"bid_1": 24.0, "ask_1": 14.0, "now": 4.0},
        },
    ]
    golden_steps: list[dict[str, Any]] = []
    for step in steps_in:
        tick = step["tick"]
        fields = {"bid_1": tick["bid_1"], "ask_1": tick["ask_1"]}
        entry_hit, entry_id = _judge_rules(sym.entry_strategies, sym.entry_strategy_logic, fields)
        exit_hit, exit_id = _judge_rules(sym.exit_strategies, sym.exit_strategy_logic, fields)
        golden_steps.append(
            {
                "label": step["label"],
                "entry_hit": entry_hit,
                "entry_rule_id": entry_id,
                "exit_hit": exit_hit,
                "exit_rule_id": exit_id,
            }
        )
    _write_pair(
        sid,
        {
            "id": sid,
            "module": "strategy_judge",
            "fixture": "strategy_config_us_fixture.json",
            "symbol_index": 0,
            "steps": steps_in,
        },
        {"id": sid, "steps": golden_steps},
    )


def _scenario_strategy_judge_us_pnl_exit() -> None:
    sid = "l2_02_strategy_judge_us_pnl_exit"
    cfg = load_config(str(FIXTURES / "strategy_config_us_fixture.json"))
    sym = cfg.symbols[0]
    for rule in sym.exit_strategies:
        if rule.rule_id == "E3_CLOSE":
            rule.enabled = True
        if rule.rule_id == "E1_CLOSE":
            rule.enabled = False
    steps_in = [
        {"label": "pnl_exit_hit", "tick": {"gain_fraction": 0.12, "now": 1.0}},
        {"label": "pnl_exit_miss", "tick": {"gain_fraction": 0.05, "now": 2.0}},
    ]
    golden_steps: list[dict[str, Any]] = []
    for step in steps_in:
        gf = float(step["tick"]["gain_fraction"])
        entry_hit, entry_id = _judge_rules(sym.entry_strategies, sym.entry_strategy_logic, {}, gf)
        exit_hit, exit_id = _judge_rules(sym.exit_strategies, sym.exit_strategy_logic, {}, gf)
        golden_steps.append(
            {
                "label": step["label"],
                "entry_hit": entry_hit,
                "entry_rule_id": entry_id,
                "exit_hit": exit_hit,
                "exit_rule_id": exit_id,
            }
        )
    _write_pair(
        sid,
        {
            "id": sid,
            "module": "strategy_judge",
            "fixture": "strategy_config_us_fixture.json",
            "symbol_index": 0,
            "exit_overrides": {"enable_rule_ids": ["E3_CLOSE"], "disable_rule_ids": ["E1_CLOSE"]},
            "steps": steps_in,
        },
        {"id": sid, "steps": golden_steps},
    )


def _scenario_hk_short_band() -> None:
    sid = "l2_03_hk_short_band"
    cases_in = [
        {"ask": 36.08, "bid": 36.07},
        {"ask": 36.08, "bid": 36.0},
        {"ask": 36.08, "bid": 35.89},
        {"ask": 36.08, "bid": 35.70},
        {"ask": 36.08, "ask_in_band": 36.062},
        {"ask": 36.08, "ask_in_band": 36.10},
    ]
    pct = HK_SHORT_ASK_BAND_PCT
    golden_cases: list[dict[str, Any]] = []
    for c in cases_in:
        ask = float(c["ask"])
        lo = ask * (1.0 - pct)
        hi = ask
        out: dict[str, Any] = {
            "ask": ask,
            "band_lo": lo,
            "band_hi": hi,
        }
        if "bid" in c:
            bid = float(c["bid"])
            out["bid"] = bid
            out["bid_in_band"] = lo <= bid <= hi
        if "ask_in_band" in c:
            px = float(c["ask_in_band"])
            out["ask_in_band"] = px
            out["ask_in_original_band"] = lo <= px <= hi
        golden_cases.append(out)
    _write_pair(
        sid,
        {"id": sid, "module": "hk_short_band", "cases": cases_in},
        {"id": sid, "cases": golden_cases},
    )


def _scenario_staged_take_profit() -> None:
    sid = "l2_07_staged_take_profit"
    cases_in = [
        {"opened_qty": 563.0, "initial_qty": 199.0, "fractions": [0.5, 0.5], "lot_size": 1},
        {"opened_qty": 200.0, "initial_qty": 563.0, "fractions": [0.5, 0.5], "lot_size": 1},
        {"opened_qty": 600.0, "initial_qty": 600.0, "fractions": [0.5, 0.5], "lot_size": 200},
        {"opened_qty": 563.0, "initial_qty": 563.0, "fractions": [0.5, 0.3, 0.2], "lot_size": 1},
        {"opened_qty": 500.0, "initial_qty": 500.0, "fractions": [0.5, 0.5], "lot_size": 200},
    ]

    class _Sym:
        def __init__(self, opened: float, initial: float | None) -> None:
            self.opened_qty = opened
            self.initial_qty = initial

    golden_cases = []
    for c in cases_in:
        baseline = resolve_staged_baseline_qty(_Sym(c["opened_qty"], c["initial_qty"]))
        qtys = allocate_tranche_qtys(baseline, c["fractions"], c["lot_size"])
        golden_cases.append(
            {
                "baseline_qty": baseline,
                "qtys": qtys,
                "sum_qty": sum(qtys),
            }
        )
    _write_pair(
        sid,
        {"id": sid, "module": "staged_take_profit", "cases": cases_in},
        {"id": sid, "cases": golden_cases},
    )


def _scenario_book_guard_emergency_ref() -> None:
    sid = "l2_06_book_guard_emergency_ref"
    cases_in = [
        {
            "side": "SELL",
            "bid_1": 115.05,
            "ask_1": 114.82,
            "opened_price": 113.64,
            "last_price": 0.0,
        },
        {
            "side": "SELL",
            "bid_1": 0.0,
            "ask_1": 0.0,
            "opened_price": 113.64,
        },
        {
            "side": "SELL",
            "code": "US.AMD",
            "ref_px": 486.2705,
        },
    ]
    golden_cases = []
    for c in cases_in:
        if "ref_px" in c:
            golden_cases.append(
                {
                    "submit_px": _emergency_limit_submit_px(
                        float(c["ref_px"]), str(c.get("side", "SELL")), str(c.get("code", "US.AMD"))
                    )
                }
            )
            continue
        px, src = emergency_exit_ref_px(
            side=str(c["side"]),
            bid_1=float(c["bid_1"]),
            ask_1=float(c["ask_1"]),
            opened_price=float(c["opened_price"]),
            quote_ctx=None,
            trade_code="US.SPCX",
        )
        golden_cases.append({"price": px, "source": src})
    _write_pair(
        sid,
        {"id": sid, "module": "book_guard_ref", "cases": cases_in},
        {"id": sid, "cases": golden_cases},
    )


def _write_manifest(scenario_ids: list[str]) -> None:
    manifest = {"version": 1, "scenarios": scenario_ids}
    path = REPLAY / "l2_manifest.json"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("wrote", path)


def main() -> int:
    scenario_ids = [
        "l2_01_strategy_judge_us_orderbook",
        "l2_02_strategy_judge_us_pnl_exit",
        "l2_03_hk_short_band",
        "l2_06_book_guard_emergency_ref",
        "l2_07_staged_take_profit",
    ]
    _scenario_strategy_judge_us_orderbook()
    _scenario_strategy_judge_us_pnl_exit()
    _scenario_hk_short_band()
    _scenario_staged_take_profit()
    _scenario_book_guard_emergency_ref()
    _write_manifest(scenario_ids)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

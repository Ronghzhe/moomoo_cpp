#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OpenD trade API rate-limit helpers for benchmark scripts."""

from __future__ import annotations

import time
from typing import Any, Callable, TypeVar

from bench_common import setup_python_path

setup_python_path()

from order_errors import is_operating_too_fast_err, is_order_rate_limit_err  # noqa: E402

T = TypeVar("T")


def is_rate_limit_detail(detail: str) -> bool:
    raw = str(detail or "")
    return is_order_rate_limit_err(raw) or is_operating_too_fast_err(raw)


def rate_limit_wait_sec(attempt: int, base_wait_sec: float) -> float:
    # attempt starts at 1
    return max(base_wait_sec, base_wait_sec * attempt)


def call_with_rate_limit_retry(
    fn: Callable[[], tuple[int, Any]],
    *,
    label: str,
    max_retries: int,
    base_wait_sec: float,
    pre_sleep_sec: float = 0.0,
) -> tuple[int, Any, int]:
    """
    Call trade API fn() -> (ret, data).
    Returns (ret, data, attempts_used).
    """
    if pre_sleep_sec > 0:
        time.sleep(pre_sleep_sec)
    last_ret = -1
    last_out: Any = None
    attempts = 0
    for attempt in range(1, max_retries + 2):
        attempts = attempt
        last_ret, last_out = fn()
        if last_ret == 0:
            return last_ret, last_out, attempts
        detail = str(last_out)
        if not is_rate_limit_detail(detail) or attempt > max_retries:
            return last_ret, last_out, attempts
        wait = rate_limit_wait_sec(attempt, base_wait_sec)
        print(
            f"[rate_limit] {label} hit limit attempt={attempt}/{max_retries} "
            f"wait={wait:.1f}s detail={detail[:120]}",
            flush=True,
        )
        time.sleep(wait)
    return last_ret, last_out, attempts

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Print REAL bench prices as a single stdout line: BUY SELL PWD_MD5"""

from __future__ import annotations

import sys

from bench_common import setup_python_path

setup_python_path()

from bench_real_roundtrip import fetch_snapshot, pick_limit_prices, resolve_trade_password_md5  # noqa: E402


def main() -> int:
    code = sys.argv[1] if len(sys.argv) > 1 else "HK.02208"
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 11121
    pwd, md5 = resolve_trade_password_md5()
    if not pwd or not md5:
        print("missing trade password", file=sys.stderr)
        return 2
    snap = fetch_snapshot(code, host, port)
    buy, sell = pick_limit_prices(snap)
    # Prefix marker: moomoo SDK also prints connect/disconnect logs to stdout.
    print(f"PRICES\t{buy}\t{sell}\t{md5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env bash
# SIMULATE query (GetOrderList) + cancel (ModifyOrder) benchmark — Python vs C++.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY="/home/ubuntu/workspace/venv/bin/python"
CFG="${ROOT}/config/bench_query_cancel.yaml"
OUT_DIR="${ROOT}/reports/opend_bench_query_cancel_$(date +%Y%m%d)"
mkdir -p "${OUT_DIR}"

export LD_LIBRARY_PATH="/home/ubuntu/workspace/MMAPI4CPP_10.9.6908/Bin/Centos7:${LD_LIBRARY_PATH:-}"

CPP_BIN="${ROOT}/cpp/build/bench_query_cancel"
if [ ! -x "${CPP_BIN}" ]; then
  cd "${ROOT}/cpp/build" && cmake --build . -j "$(nproc)" --target bench_query_cancel
fi

PY_OUT="${OUT_DIR}/py_query_cancel.jsonl"
CPP_OUT="${OUT_DIR}/cpp_query_cancel.jsonl"

echo "[query_cancel] Python..."
"${PY}" "${ROOT}/python/bench_query_cancel.py" --config "${CFG}" --out "${PY_OUT}"

echo "[query_cancel] C++..."
"${CPP_BIN}" \
  --host 127.0.0.1 --port 11121 \
  --code US.AAPL --qty 1 --price 0.01 \
  --warm-iters 10 --api-gap-sec 0.5 --iter-gap-sec 3.0 \
  --out "${CPP_OUT}"

echo "[query_cancel] stats..."
"${PY}" - <<PY
import json
from pathlib import Path

def load(p):
    rows = []
    for line in Path(p).read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows

def pct(vals, q):
    s = sorted(vals)
    if not s:
        return 0.0
    i = max(0, min(len(s)-1, int(round((len(s)-1)*q))))
    return s[i]

def stats(rows, field, pred=lambda r: True):
    vals = [float(r[field]) for r in rows if pred(r) and r.get(field) is not None]
    if not vals:
        return None
    return {
        "n": len(vals),
        "p50": round(pct(vals, 0.5), 2),
        "p90": round(pct(vals, 0.9), 2),
        "mean": round(sum(vals)/len(vals), 2),
        "min": round(min(vals), 2),
        "max": round(max(vals), 2),
    }

for name, path in [("python", "${PY_OUT}"), ("cpp", "${CPP_OUT}")]:
    rows = load(path)
    ok = sum(1 for r in rows if r.get("ok"))
    print(f"=== {name} ok={ok}/{len(rows)} ===")
    for field in ("ms_query", "ms_cancel"):
        s = stats(rows, field, lambda r, f=field: r.get("ok_query" if f=="ms_query" else "ok_cancel", r.get("ok")))
        print(f"  {field}: {s}")
PY

echo "[query_cancel] done"
echo "  python: ${PY_OUT}"
echo "  cpp:    ${CPP_OUT}"

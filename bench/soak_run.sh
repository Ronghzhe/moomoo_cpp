#!/usr/bin/env bash
# Test 6 — 4h stability soak (US paper bench loop)
set -euo pipefail

ROOT="/home/ubuntu/workspace/moomoo_python"
CPP="/home/ubuntu/workspace/moomoo_CPP"
PY="/home/ubuntu/workspace/venv/bin/python"
HOURS="${SOAK_HOURS:-4}"
SLEEP_SEC="${SOAK_SLEEP_SEC:-600}"
CONFIG="$CPP/bench/config/task_bench_us_paper.json"
LOG_DIR="$CPP/strategy/task_system/bench_logs"
STAMP="$(date -u +%Y%m%d_%H%M%S)"
SOAK_ID="soak_${STAMP}"
LOG="$LOG_DIR/soak_run_${STAMP}.log"
END_TS=$(( $(date +%s) + HOURS * 3600 ))
ROUND=0

mkdir -p "$LOG_DIR"
echo "soak_id=$SOAK_ID hours=$HOURS sleep_sec=$SLEEP_SEC log=$LOG" | tee "$LOG"

while [ "$(date +%s)" -lt "$END_TS" ]; do
  ROUND=$((ROUND + 1))
  echo "[soak] round=$ROUND start $(date -Is)" | tee -a "$LOG"
  BATCH_ID="$(date +%Y%m%d_%H%M%S)"
  set +e
  (cd "$CPP" && "$PY" bench/task_latency_batch_submit.py --config "$CONFIG") 2>&1 | tee -a "$LOG"
  RC=${PIPESTATUS[0]}
  set -e
  # batch script generates its own batch_id; extract last one from log line if present
  LAST_BATCH="$(grep -oE 'batch_id=[0-9]{8}_[0-9]{6}' "$LOG" | tail -1 | cut -d= -f2 || true)"
  if [ -z "$LAST_BATCH" ]; then
    LAST_BATCH="$BATCH_ID"
  fi
  "$PY" "$CPP/bench/soak_sampler.py" \
    --soak-id "$SOAK_ID" \
    --round "$ROUND" \
    --batch-id "$LAST_BATCH" \
    --round-exit-code "$RC" 2>&1 | tee -a "$LOG"
  if [ "$(date +%s)" -ge "$END_TS" ]; then
    break
  fi
  echo "[soak] round=$ROUND done rc=$RC sleep ${SLEEP_SEC}s" | tee -a "$LOG"
  sleep "$SLEEP_SEC"
done

"$PY" "$CPP/bench/soak_analyze.py" --soak-id "$SOAK_ID" --hours "$HOURS" 2>&1 | tee -a "$LOG"
echo "[soak] finished soak_id=$SOAK_ID rounds=$ROUND" | tee -a "$LOG"

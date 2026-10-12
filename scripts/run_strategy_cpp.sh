#!/usr/bin/env bash
# Deprecated wrapper — use ../run_strategy.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec "${ROOT}/run_strategy.sh" "$@"

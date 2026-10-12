#!/usr/bin/env bash
exec "$(cd "$(dirname "$0")" && pwd)/strategy/task_system/script/monitor.sh" "$@"

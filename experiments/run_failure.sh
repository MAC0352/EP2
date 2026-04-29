#!/usr/bin/env bash
# T14 — Detecção de falha: derruba o agente alvo durante a execução do
# manager e mede o tempo até a transição UP→DOWN ser registrada no log.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PORT=${PORT:-19301}
INTERVAL=${INTERVAL:-1}
TIMEOUT_MS=${TIMEOUT_MS:-500}
THRESHOLD=${THRESHOLD:-3}
OUT="$HERE/results/failure.csv"

cd "$ROOT"
make -s
mkdir -p "$HERE/results"

conf="/tmp/fail_test.conf"
echo "victim 127.0.0.1 $PORT" > "$conf"

run_trial() {
  local trial=$1
  local mgr_log="/tmp/fail_mgr_${trial}.log"

  "$ROOT/bin/agent" "$PORT" --id victim >/dev/null 2>&1 &
  local apid=$!
  sleep 0.3

  "$ROOT/bin/manager" "$conf" --interval "$INTERVAL" \
        --timeout-ms "$TIMEOUT_MS" --threshold "$THRESHOLD" \
        --no-tui > "$mgr_log" 2>&1 &
  local mpid=$!

  # Espera primeiro UP estabilizar.
  for _ in $(seq 1 30); do
    grep -q "estado UP" "$mgr_log" && break
    sleep 0.1
  done

  local t_kill_ns
  t_kill_ns=$(date +%s%N)
  kill "$apid" 2>/dev/null || true
  wait "$apid" 2>/dev/null || true

  # Espera transição DOWN.
  for _ in $(seq 1 200); do
    grep -q "estado DOWN" "$mgr_log" && break
    sleep 0.1
  done
  local t_down_ns
  t_down_ns=$(date +%s%N)

  kill "$mpid" 2>/dev/null || true
  wait "$mpid" 2>/dev/null || true

  if ! grep -q "estado DOWN" "$mgr_log"; then
    echo "$trial,NA,NA"
    return
  fi
  awk -v k="$t_kill_ns" -v d="$t_down_ns" -v iv="$INTERVAL" \
      -v to="$TIMEOUT_MS" -v th="$THRESHOLD" \
      'BEGIN { printf "%d,%.3f,%d,%d,%d\n",
               '"$trial"', (d-k)/1e9, iv, to, th }'
}

{
  echo "trial,detect_s,interval_s,timeout_ms,threshold"
  for t in 1 2 3; do run_trial "$t"; done
} | tee "$OUT"

echo "→ resultado: $OUT"

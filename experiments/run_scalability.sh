#!/usr/bin/env bash
# T13 — Escalabilidade: sobe K agentes locais (portas distintas), roda o
# manager por DURATION segundos, mede CPU% médio do manager (via
# /proc/<pid>/stat) e amostras coletadas por agente.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DURATION=${DURATION:-15}
INTERVAL=${INTERVAL:-1}
OUT="$HERE/results/scalability.csv"

cd "$ROOT"
make -s
mkdir -p "$HERE/results"

# Lê ticks/s do kernel para converter jiffies → segundos.
HZ=$(getconf CLK_TCK)

cpu_pct() {
  # CPU% acumulado de um pid em um intervalo medido externamente.
  local pid=$1 dur=$2
  local s1 s2 u1 u2 sy1 sy2
  read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u1 sy1 _ < /proc/$pid/stat
  sleep "$dur"
  read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u2 sy2 _ < /proc/$pid/stat
  awk -v du=$((u2-u1)) -v ds=$((sy2-sy1)) -v hz=$HZ -v dur=$dur \
      'BEGIN { printf "%.2f", 100.0 * (du+ds) / (hz*dur) }'
}

run_one() {
  local K=$1
  local conf="/tmp/scal_${K}.conf"
  local pids=()
  : > "$conf"
  for i in $(seq 1 "$K"); do
    local port=$((19200 + i))
    "$ROOT/bin/agent" "$port" --id "n$i" >/dev/null 2>&1 &
    pids+=($!)
    echo "n$i 127.0.0.1 $port" >> "$conf"
  done
  sleep 0.3

  rm -f history.csv
  "$ROOT/bin/manager" "$conf" --interval "$INTERVAL" \
        --timeout-ms 1000 --threshold 3 --no-tui >/dev/null 2>&1 &
  local mgr=$!
  sleep 1
  local cpu
  cpu=$(cpu_pct "$mgr" "$DURATION")
  local samples
  samples=$(($(wc -l < history.csv) - 1))

  kill "$mgr" "${pids[@]}" 2>/dev/null || true
  wait 2>/dev/null || true

  echo "$K,$DURATION,$cpu,$samples"
}

{
  echo "K_agents,duration_s,manager_cpu_pct,samples"
  for K in 1 5 10 20; do
    run_one "$K"
  done
} | tee "$OUT"

echo "→ resultado: $OUT"

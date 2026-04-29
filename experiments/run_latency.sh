#!/usr/bin/env bash
# T12 — Latência: mede RTT de GET/RESPONSE para N = 1, 10, 100 OIDs.
# Sobe um agente local, roda bin/latency e grava resultados em CSV.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PORT=${PORT:-19101}
OUT="$HERE/results/latency.csv"

cd "$ROOT"
make -s

"$ROOT/bin/agent" "$PORT" --id lat-target >/tmp/lat_agent.log 2>&1 &
AGENT=$!
trap 'kill $AGENT 2>/dev/null || true' EXIT
sleep 0.3

mkdir -p "$HERE/results"
{
  echo "N,ok,min_us,p50_us,p95_us,max_us,mean_us"
  for N in 1 10 100 1000; do
    line=$("$ROOT/bin/latency" 127.0.0.1 "$PORT" "$N" | grep '^CSV,' | cut -d, -f2-)
    echo "$line"
  done
} | tee "$OUT"

echo "→ resultado: $OUT"

#!/usr/bin/env bash
# T15 — Overhead de rede: estima bytes/segundo trocados entre manager e
# agente. Preferência: tcpdump na porta do agente; fallback: contadores
# de /proc/<pid>/net/netstat e a contagem de respostas observadas.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PORT=${PORT:-19401}
DURATION=${DURATION:-10}
INTERVAL=${INTERVAL:-1}
OUT="$HERE/results/overhead.csv"

cd "$ROOT"
make -s
mkdir -p "$HERE/results"

conf="/tmp/over_test.conf"
echo "ohnode 127.0.0.1 $PORT" > "$conf"

"$ROOT/bin/agent" "$PORT" --id ohnode >/dev/null 2>&1 &
APID=$!
trap 'kill $APID 2>/dev/null || true' EXIT
sleep 0.3

PCAP="/tmp/over_${PORT}.pcap"
TCPDUMP_OK=0
if command -v tcpdump >/dev/null 2>&1; then
  if tcpdump -i lo -w "$PCAP" -U "tcp port $PORT" \
       >/tmp/over_tcpdump.log 2>&1 &
  then
    TCPDPID=$!
    sleep 0.3
    kill -0 "$TCPDPID" 2>/dev/null && TCPDUMP_OK=1
  fi
fi

rm -f history.csv
"$ROOT/bin/manager" "$conf" --interval "$INTERVAL" --timeout-ms 1000 \
      --threshold 3 --no-tui >/dev/null 2>&1 &
MPID=$!

sleep "$DURATION"

kill "$MPID" 2>/dev/null || true
wait "$MPID" 2>/dev/null || true

samples=$(($(wc -l < history.csv) - 1))

if [ "$TCPDUMP_OK" = "1" ]; then
  kill "$TCPDPID" 2>/dev/null || true
  wait "$TCPDPID" 2>/dev/null || true
  total_bytes=$(tcpdump -r "$PCAP" -nn -tt 2>/dev/null \
                 | awk '{ for (i=1;i<=NF;i++) if ($i=="length") { print $(i+1); break } }' \
                 | awk '{s+=$1} END{print s+0}')
  src="tcpdump"
else
  total_bytes="NA"
  src="unavailable"
fi

{
  echo "duration_s,interval_s,samples,total_bytes,bytes_per_s,source"
  if [ "$total_bytes" = "NA" ]; then
    echo "$DURATION,$INTERVAL,$samples,NA,NA,$src"
  else
    bps=$(awk -v b="$total_bytes" -v d="$DURATION" 'BEGIN{printf "%.1f", b/d}')
    echo "$DURATION,$INTERVAL,$samples,$total_bytes,$bps,$src"
  fi
} | tee "$OUT"

echo "→ resultado: $OUT  (pcap: $PCAP)"

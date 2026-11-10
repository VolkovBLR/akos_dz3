#!/bin/sh
# Независимые процессы: named POSIX-семафоры + очереди сообщений.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
SESS=${SESS:-akosdz3mq}
CONF=${CONF:-configs/short.conf}
LOG=${LOG:-tests/mq.log}

./mq_judge --create -c "$CONF" -S "$SESS" -l "$LOG" -q &
JPID=$!
sleep 0.15
./mq_log -c "$CONF" -S "$SESS" -l "$LOG" -q &
i=0
while [ "$i" -lt 4 ]; do
    ./mq_car -c "$CONF" -S "$SESS" --id "$i" -q &
    i=$((i + 1))
done
wait "$JPID"
wait 2>/dev/null || true

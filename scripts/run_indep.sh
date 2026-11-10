#!/bin/sh
# Запуск независимых процессов named-sem + shm.
# Судья создаёт IPC, логгер и машины присоединяются отдельно.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
SESS=${SESS:-akosdz3}
CONF=${CONF:-configs/short.conf}
LOG=${LOG:-tests/indep.log}

./race_judge --create -c "$CONF" -S "$SESS" -l "$LOG" -q &
JPID=$!
sleep 0.15
./race_log -c "$CONF" -S "$SESS" -l "$LOG" -q &
LPID=$!
# число машин из конфига: teams * cars_per_team (short.conf = 2*2)
i=0
while [ "$i" -lt 4 ]; do
    ./race_car -c "$CONF" -S "$SESS" --id "$i" -q &
    i=$((i + 1))
done
wait "$JPID"
wait "$LPID" 2>/dev/null || true
wait 2>/dev/null || true

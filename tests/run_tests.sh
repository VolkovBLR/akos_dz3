#!/bin/sh
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

UNAME=$(uname -s)

[ -x ./race_named ] || fail "нет бинарника race_named"

echo "== тест 1: короткая гонка (fork + named POSIX sem + shm) =="
./race_named -c configs/short.conf -S tnamed -l tests/short.log -q > tests/short.out
grep -q "ИТОГОВАЯ КЛАССИФИКАЦИЯ МАШИН" tests/short.out || fail "нет классификации в stdout"
grep -q "КЛАССИФИКАЦИЯ КОМАНД" tests/short.out || fail "нет командной классификации"
grep -q "ИТОГОВАЯ КЛАССИФИКАЦИЯ МАШИН" tests/short.log || fail "нет классификации в журнале"
grep -q "Судья: гонка началась" tests/short.out || fail "нет старта"
if grep -E 'маршрут \(стоимость [0-9]+\):[[:space:]]*$' tests/short.out >/dev/null; then
    fail "пустой маршрут при ненулевой стоимости"
fi

echo "== тест 2: воспроизводимость seed =="
./race_named -n 2 -p 2 -k 1 -m 20 -s 7 -d 0 -q -S tseed1 -l tests/seed1.log > tests/seed1.out
./race_named -n 2 -p 2 -k 1 -m 20 -s 7 -d 0 -q -S tseed2 -l tests/seed2.log > tests/seed2.out
tail -n 12 tests/seed1.out > tests/seed1.tail
tail -n 12 tests/seed2.out > tests/seed2.tail
cmp tests/seed1.tail tests/seed2.tail || fail "одинаковый seed дал разный итог"

echo "== тест 3: справка =="
./race_named -h | grep -q "Использование" || fail "нет справки"

[ -x ./race_judge ] && [ -x ./race_car ] && [ -x ./race_log ] || fail "нет независимых бинарников"
echo "== тест 4: независимые процессы named sem + shm =="
chmod +x scripts/run_indep.sh scripts/cleanup_ipc.sh
SESS=tindep CONF=configs/short.conf LOG=tests/indep.log sh scripts/run_indep.sh > tests/indep.out
grep -q "ИТОГОВАЯ КЛАССИФИКАЦИЯ МАШИН" tests/indep.out tests/indep.log || fail "indep без классификации"

if [ "$UNAME" = Linux ]; then
    [ -x ./race_unnamed ] || fail "нет race_unnamed"
    echo "== тест 5: unnamed POSIX sem в shm =="
    ./race_unnamed -c configs/short.conf -S tunnamed -l tests/unnamed.log -q > tests/unnamed.out
    grep -q "ИТОГОВАЯ КЛАССИФИКАЦИЯ МАШИН" tests/unnamed.out || fail "unnamed без классификации"

    if [ -x ./mq_judge ]; then
        echo "== тест 6: независимые процессы named sem + mq =="
        chmod +x scripts/run_mq.sh
        SESS=tmq CONF=configs/short.conf LOG=tests/mq.log sh scripts/run_mq.sh > tests/mq.out
        grep -q "ИТОГОВАЯ КЛАССИФИКАЦИЯ МАШИН" tests/mq.out tests/mq.log || fail "mq без классификации"
    fi
fi

echo "Все доступные тесты прошли."

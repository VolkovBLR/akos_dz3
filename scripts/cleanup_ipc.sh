#!/bin/sh
# Снятие оставшихся POSIX shm/sem/mq после аварийного выхода.
SESS=${1:-akosdz3}
rm -f "/dev/shm/${SESS}_shm" 2>/dev/null || true
for tag in board logmu done logq ready t0 t1 t2 t3 t4 t5 t6 t7 t8 t9 t10 t11 t12 t13 t14 t15 \
           mqlog mqdone mqc0 mqc1 mqc2 mqc3 mqc4 mqc5 mqc6 mqc7; do
    rm -f "/dev/shm/sem.${SESS}_${tag}" "/dev/mqueue/${SESS}_${tag}" 2>/dev/null || true
done
# macOS named semaphores
if command -v python3 >/dev/null 2>&1; then
    python3 - "$SESS" <<'PY' 2>/dev/null || true
import ctypes, sys
name = sys.argv[1]
libc = ctypes.CDLL(None)
for tag in ["board","logmu","done","logq","ready"] + ["t%d"%i for i in range(16)]:
    libc.sem_unlink(("/"+name+"_"+tag).encode())
libc.shm_unlink(("/"+name+"_shm").encode())
PY
fi
echo "cleanup $SESS done"

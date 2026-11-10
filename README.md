# Индивидуальное задание № 3 — вариант 20 «Автогонки»

Консольная имитация гонки по правилам В. Пудова («Наука и жизнь», № 12, 1983)
на **взаимодействующих процессах** для ОС Linux (POSIX shm, семафоры, mq).

## Сборка

```bash
make          # на Linux — все варианты; на macOS — named + независимые shm
make test
```

| Программа | Оценка | IPC |
|---|---|---|
| `race_unnamed` | 4–5 | родитель `fork`, **неименованные** POSIX-семафоры в shm, POSIX shm |
| `race_named` | 6 | родитель `fork`, **именованные** POSIX-семафоры, POSIX shm |
| `race_judge` / `race_car` / `race_log` | 7–8 | независимый запуск, named sem + shm |
| `mq_judge` / `mq_car` / `mq_log` | 8+ | независимый запуск, named sem + **POSIX очереди сообщений** |

Неименованные семафоры с `pshared=1` и POSIX mq рассчитаны на Linux.
На macOS собирайте и проверяйте `race_named` и тройку `race_judge`/`race_car`/`race_log`.

## Запуск

```bash
./race_named
./race_named -c configs/default.conf
./race_named -n 3 -p 2 -k 2 -s 1 -d 40 -l race.log
./race_named -i
./race_unnamed -c configs/short.conf -q

# независимые процессы (сначала судья создаёт IPC)
SESS=demo sh scripts/run_indep.sh
SESS=demo sh scripts/run_mq.sh          # Linux

# ручной независимый запуск
./race_judge --create -c configs/short.conf -S demo -q &
./race_log   -c configs/short.conf -S demo -q &
./race_car   -c configs/short.conf -S demo --id 0 -q &
# ... --id 1, 2, 3 для short.conf (2 команды × 2 машины)
```

Завершение: финиш/сход всех машин, предел раундов, `Ctrl+C` (SIGINT/SIGTERM).
Семафоры, shm и очереди снимаются при выходе; аварийно: `scripts/cleanup_ipc.sh [сессия]`.

Подробности — в `report.md`.

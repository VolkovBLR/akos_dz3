# Вариант 20: четыре схемы IPC (unnamed sem, named sem, независимые shm, mq).
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
INC = -Isrc
UNAME_S := $(shell uname -s)
LDFLAGS =
ifeq ($(UNAME_S),Linux)
  LDFLAGS += -lrt -pthread
else
  LDFLAGS += -pthread
endif

COMMON = src/track.c src/game.c src/ipc.c src/config.c

.PHONY: all clean test linux

all: race_named race_judge race_car race_log
ifeq ($(UNAME_S),Linux)
all: race_unnamed mq_judge mq_car mq_log
endif

# 4–5 / 6 баллов: родитель fork + POSIX shm
race_unnamed: $(COMMON) src/main_fork.c
	$(CC) $(CFLAGS) -DIPC_KIND=1 $(INC) -o $@ $(COMMON) src/main_fork.c $(LDFLAGS)

race_named: $(COMMON) src/main_fork.c
	$(CC) $(CFLAGS) -DIPC_KIND=2 $(INC) -o $@ $(COMMON) src/main_fork.c $(LDFLAGS)

# 7–8 баллов: независимые программы, named sem + shm
race_judge: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=2 -DROLE=1 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

race_car: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=2 -DROLE=2 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

race_log: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=2 -DROLE=3 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

# выше 8: независимые программы, named sem + POSIX mq
mq_judge: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=3 -DROLE=1 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

mq_car: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=3 -DROLE=2 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

mq_log: $(COMMON) src/main_role.c
	$(CC) $(CFLAGS) -DIPC_KIND=3 -DROLE=3 $(INC) -o $@ $(COMMON) src/main_role.c $(LDFLAGS)

linux: race_unnamed race_named race_judge race_car race_log mq_judge mq_car mq_log

clean:
	rm -f race_unnamed race_named race_judge race_car race_log \
		mq_judge mq_car mq_log race.log tests/*.out tests/*.log tests/*.tail

test: all
	sh tests/run_tests.sh

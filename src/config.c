#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void options_defaults(Options *o)
{
    memset(o, 0, sizeof(*o));
    o->teams = 3;
    o->cars_per_team = 2;
    o->laps_to_go = 2;
    o->max_rounds = 80;
    o->lanes = 4;
    o->rows = 12;
    o->cols = 20;
    o->seed = 1;
    o->delay_ms = 0;
    o->bonuses_on = 1;
    o->lap_retire_on = 0;
    o->stall_retire_on = 1;
    o->quiet = 0;
    o->car_id = -1;
    strncpy(o->logfile, "race.log", sizeof(o->logfile) - 1);
    strncpy(o->session, "akosdz3", sizeof(o->session) - 1);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0')
        return -1;
    *out = (int)v;
    return 0;
}

void usage(const char *argv0)
{
    char buf[1600];
    int n = snprintf(buf, sizeof(buf),
        "Использование: %s [опции]\n"
        "  -c файл     конфигурационный файл (ключ=значение)\n"
        "  -s seed     начальное значение ГПСЧ\n"
        "  -d мс       задержка отображения события\n"
        "  -l файл     журнал результатов\n"
        "  -n команд   число команд (2..8)\n"
        "  -p машин    машин в команде (1..2)\n"
        "  -k кругов   дистанция\n"
        "  -m раундов  предел раундов\n"
        "  -g дорожек  число дорожек (2..8)\n"
        "  -R строк -C столбцов   размер поля\n"
        "  -S имя      имя сессии IPC (семафоры/shm/mq)\n"
        "  -q          без ASCII-трассы\n"
        "  -i          интерактивный ввод параметров\n"
        "  --id N      номер машины (независимый процесс car)\n"
        "  --create    создать IPC-объекты (судья)\n"
        "  --no-bonus  без круговых премий\n"
        "  --no-lap-retire  не снимать отстающих на круг\n"
        "  -h          справка\n",
        argv0);
    write_all(STDOUT_FILENO, buf, (size_t)n);
}

static void apply_kv(Options *o, const char *key, const char *val)
{
    int v;
    if (strcmp(key, "teams") == 0 && parse_int(val, &v) == 0)
        o->teams = v;
    else if (strcmp(key, "cars_per_team") == 0 && parse_int(val, &v) == 0)
        o->cars_per_team = v;
    else if (strcmp(key, "laps") == 0 && parse_int(val, &v) == 0)
        o->laps_to_go = v;
    else if (strcmp(key, "max_rounds") == 0 && parse_int(val, &v) == 0)
        o->max_rounds = v;
    else if (strcmp(key, "lanes") == 0 && parse_int(val, &v) == 0)
        o->lanes = v;
    else if (strcmp(key, "rows") == 0 && parse_int(val, &v) == 0)
        o->rows = v;
    else if (strcmp(key, "cols") == 0 && parse_int(val, &v) == 0)
        o->cols = v;
    else if (strcmp(key, "seed") == 0 && parse_int(val, &v) == 0)
        o->seed = v;
    else if (strcmp(key, "delay_ms") == 0 && parse_int(val, &v) == 0)
        o->delay_ms = v;
    else if (strcmp(key, "bonuses") == 0 && parse_int(val, &v) == 0)
        o->bonuses_on = v;
    else if (strcmp(key, "lap_retire") == 0 && parse_int(val, &v) == 0)
        o->lap_retire_on = v;
    else if (strcmp(key, "stall_retire") == 0 && parse_int(val, &v) == 0)
        o->stall_retire_on = v;
    else if (strcmp(key, "quiet") == 0 && parse_int(val, &v) == 0)
        o->quiet = v;
    else if (strcmp(key, "session") == 0)
        strncpy(o->session, val, sizeof(o->session) - 1);
    else if (strcmp(key, "log") == 0)
        strncpy(o->logfile, val, sizeof(o->logfile) - 1);
}

static int load_config(Options *o, const char *path)
{
    char buf[4096];
    ssize_t n, off = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    while (off < (ssize_t)sizeof(buf) - 1) {
        n = read(fd, buf + off, sizeof(buf) - 1 - (size_t)off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        off += n;
    }
    close(fd);
    buf[off] = '\0';
    {
        char *save = buf;
        char *line;
        while ((line = strtok(save, "\n")) != NULL) {
            char key[64], val[192];
            char *hash;
            save = NULL;
            hash = strchr(line, '#');
            if (hash)
                *hash = '\0';
            if (sscanf(line, " %63[^=]=%191s", key, val) == 2)
                apply_kv(o, key, val);
        }
    }
    return 0;
}

static int read_line_fd(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        char ch;
        ssize_t r = read(fd, &ch, 1);
        if (r == 0)
            break;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ch == '\n')
            break;
        if (ch == '\r')
            continue;
        buf[n++] = ch;
    }
    buf[n] = '\0';
    return (int)n;
}

static void prompt_int(const char *q, int *dst)
{
    char line[64];
    char out[128];
    int n = snprintf(out, sizeof(out), "%s [%d]: ", q, *dst);
    write_all(STDOUT_FILENO, out, (size_t)n);
    if (read_line_fd(STDIN_FILENO, line, sizeof(line)) > 0) {
        int v;
        if (parse_int(line, &v) == 0)
            *dst = v;
    }
}

int options_parse(Options *o, int argc, char **argv)
{
    int i;
    char err[128];
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-i") == 0) {
            o->interactive = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            o->quiet = 1;
        } else if (strcmp(argv[i], "--no-bonus") == 0) {
            o->bonuses_on = 0;
        } else if (strcmp(argv[i], "--no-lap-retire") == 0) {
            o->lap_retire_on = 0;
        } else if (strcmp(argv[i], "--create") == 0) {
            o->create = 1;
        } else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->car_id);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            strncpy(o->conf_path, argv[++i], sizeof(o->conf_path) - 1);
            if (load_config(o, o->conf_path) != 0) {
                snprintf(err, sizeof(err), "Не удалось прочитать конфиг %s\n", o->conf_path);
                write_all(STDERR_FILENO, err, strlen(err));
                return -1;
            }
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->seed);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->delay_ms);
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            strncpy(o->logfile, argv[++i], sizeof(o->logfile) - 1);
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            strncpy(o->session, argv[++i], sizeof(o->session) - 1);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->teams);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->cars_per_team);
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->laps_to_go);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->max_rounds);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->lanes);
        } else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->rows);
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            parse_int(argv[++i], &o->cols);
        } else {
            snprintf(err, sizeof(err), "Неизвестная опция: %s\n", argv[i]);
            write_all(STDERR_FILENO, err, strlen(err));
            usage(argv[0]);
            return -1;
        }
    }
    if (o->interactive) {
        prompt_int("Число команд", &o->teams);
        prompt_int("Машин в команде", &o->cars_per_team);
        prompt_int("Кругов", &o->laps_to_go);
        prompt_int("Предел раундов", &o->max_rounds);
        prompt_int("Seed", &o->seed);
        prompt_int("Задержка мс", &o->delay_ms);
    }
    return 0;
}

int options_validate(const Options *o)
{
    if (o->teams < 2 || o->teams > MAX_TEAMS)
        return -1;
    if (o->cars_per_team < 1 || o->cars_per_team > 2)
        return -1;
    if (o->laps_to_go < 1 || o->laps_to_go > 20)
        return -1;
    if (o->max_rounds < 1)
        return -1;
    if (o->lanes < 2 || o->lanes > MAX_LANES)
        return -1;
    if (o->rows < 2 * o->lanes + 2 || o->cols < 2 * o->lanes + 2)
        return -1;
    return 0;
}

void options_apply(Game *g, const Options *o)
{
    g->teams = o->teams;
    g->cars_per_team = o->cars_per_team;
    g->laps_to_go = o->laps_to_go;
    g->max_rounds = o->max_rounds;
    g->delay_ms = o->delay_ms;
    g->bonuses_on = o->bonuses_on;
    g->lap_retire_on = o->lap_retire_on;
    g->stall_retire_on = o->stall_retire_on;
    g->quiet = o->quiet;
    g->seed = o->seed;
    g->track.rows = o->rows;
    g->track.cols = o->cols;
    g->track.lanes = o->lanes;
    strncpy(g->logfile, o->logfile, sizeof(g->logfile) - 1);
    strncpy(g->session, o->session, sizeof(g->session) - 1);
}

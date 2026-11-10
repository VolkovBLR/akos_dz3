#define _POSIX_C_SOURCE 200809L


#include "game.h"
#include "ipc.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef ROLE
#define ROLE 1
#endif

static Game *g_sig;

static void on_signal(int sig)
{
    const char msg[] = "\nПолучен сигнал прерывания, завершение сессии...\n";
    (void)sig;
    write_all(STDOUT_FILENO, msg, sizeof(msg) - 1);
    if (g_sig)
        game_request_stop(g_sig, 1);
}

int main(int argc, char **argv)
{
    Options opt;
    Game *g = NULL;
    struct sigaction sa;
    int st;
    int creator = 0;

    options_defaults(&opt);
    st = options_parse(&opt, argc, argv);
    if (st == 1)
        return 0;
    if (st != 0 || options_validate(&opt) != 0) {
        const char *m = "Некорректные параметры. См. -h.\n";
        write_all(STDERR_FILENO, m, strlen(m));
        return 1;
    }
#if ROLE == 2
    if (opt.car_id < 0 || opt.car_id >= opt.teams * opt.cars_per_team) {
        const char *m = "Для машины укажите --id N.\n";
        write_all(STDERR_FILENO, m, strlen(m));
        return 1;
    }
#endif

#if ROLE == 1
    creator = 1;
    if (ipc_create(&g, &opt) != 0) {
        const char *m = "Судья: не удалось создать IPC.\n";
        write_all(STDERR_FILENO, m, strlen(m));
        return 1;
    }
#else
    if (ipc_attach(&g, &opt, 8000) != 0) {
        const char *m = "Не удалось присоединиться к сессии (судья уже запущен?).\n";
        write_all(STDERR_FILENO, m, strlen(m));
        return 1;
    }
#endif

    g_sig = g;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

#if ROLE == 1
    /* ждём логгер + все машины */
    ready_wait_n(g, g->n_cars + 1);
    judge_loop(g);
    /* не снимать shm/sem/mq, пока рабочие процессы не вышли из циклов */
    (void)ready_wait_n_timeout(g, g->n_cars + 1, 8000);
    ipc_cleanup(g, 1);
#elif ROLE == 2
    ready_post(g);
    car_loop(g, opt.car_id);
    ready_post(g);
    ipc_cleanup(g, 0);
#else
    ready_post(g);
    logger_loop(g);
    ready_post(g);
    ipc_cleanup(g, 0);
#endif
    (void)creator;
    return 0;
}

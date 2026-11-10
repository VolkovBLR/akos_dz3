#define _POSIX_C_SOURCE 200809L


#include "game.h"
#include "ipc.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static Game *g_sig;
static pid_t child_pids[MAX_CARS + 4];
static int n_children;

static void on_signal(int sig)
{
    const char msg[] = "\nПолучен сигнал прерывания, судья завершает гонку...\n";
    (void)sig;
    write_all(STDOUT_FILENO, msg, sizeof(msg) - 1);
    if (g_sig)
        game_request_stop(g_sig, 1);
}

static int spawn(void (*fn)(Game *, int), Game *g, int arg)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        fn(g, arg);
        _exit(0);
    }
    child_pids[n_children++] = pid;
    return 0;
}

static void run_logger(Game *g, int unused)
{
    (void)unused;
    logger_loop(g);
}

static void run_car(Game *g, int id)
{
    car_loop(g, id);
}

int main(int argc, char **argv)
{
    Options opt;
    Game *g = NULL;
    struct sigaction sa;
    int i, st;
    int rc = 0;
    char err[160];

    options_defaults(&opt);
    st = options_parse(&opt, argc, argv);
    if (st == 1)
        return 0;
    if (st != 0 || options_validate(&opt) != 0) {
        const char *m = "Некорректные параметры. См. -h.\n";
        write_all(STDERR_FILENO, m, strlen(m));
        return 1;
    }

    if (ipc_create(&g, &opt) != 0) {
        snprintf(err, sizeof(err),
                 "Не удалось создать IPC (shm/семафоры): %s. "
                 "На macOS unnamed pshared семафоры недоступны — собирайте race_named.\n",
                 strerror(errno));
        write_all(STDERR_FILENO, err, strlen(err));
        return 1;
    }
    g_sig = g;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (spawn(run_logger, g, 0) != 0) {
        rc = 1;
        goto done;
    }
    for (i = 0; i < g->n_cars; i++) {
        if (spawn(run_car, g, i) != 0) {
            rc = 1;
            game_request_stop(g, 1);
            goto done;
        }
    }

    judge_loop(g);

done:
    for (i = 0; i < n_children; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }
    ipc_cleanup(g, 1);
    return rc;
}

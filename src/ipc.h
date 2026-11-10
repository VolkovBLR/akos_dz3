#ifndef IPC_H
#define IPC_H



#include "game.h"

#include <stddef.h>

typedef struct {
    int teams;
    int cars_per_team;
    int laps_to_go;
    int max_rounds;
    int delay_ms;
    int bonuses_on;
    int lap_retire_on;
    int stall_retire_on;
    int quiet;
    int seed;
    int rows, cols, lanes;
    char logfile[256];
    char session[64];
    char conf_path[256];
    int interactive;
    int car_id;     
    int create;    
    int role;     
} Options;

void options_defaults(Options *o);
int options_parse(Options *o, int argc, char **argv);
int options_validate(const Options *o);
void options_apply(Game *g, const Options *o);
void usage(const char *argv0);

/* shm + семафоры */
int ipc_create(Game **out, const Options *o);
int ipc_attach(Game **out, const Options *o, int timeout_ms);
void ipc_cleanup(Game *g, int creator);

void board_lock(Game *g);
void board_unlock(Game *g);
void log_lock(Game *g);
void log_unlock(Game *g);
void car_turn_wait(Game *g, int car_id);
void car_turn_post(Game *g, int car_id);
void done_wait(Game *g);
void done_post(Game *g);
void log_item_wait(Game *g);
void log_item_post(Game *g);
void ready_post(Game *g);
void ready_wait_n(Game *g, int n);
/* 0 — дождались n сигналов, -1 — истек timeout_ms. */
int ready_wait_n_timeout(Game *g, int n, int timeout_ms);
void wake_all_for_shutdown(Game *g);

#if IPC_KIND == 3
int mq_init_create(const Options *o);
int mq_init_attach(const Options *o, int timeout_ms);
void mq_cleanup(int creator, int n_cars);
int mq_send_turn(int car_id, const Game *g);
int mq_recv_turn(Game *g, int car_id);
int mq_send_done(const Game *g);
int mq_recv_done(Game *g);
int mq_send_log(const char *text);
int mq_recv_log(char *text, size_t cap);
#endif

#endif

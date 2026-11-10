#ifndef GAME_H
#define GAME_H

#include "track.h"

#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>

#ifndef IPC_KIND
#define IPC_KIND 2
#endif

typedef enum {
    CAR_RACING = 0,
    CAR_ZERO_PENDING,
    CAR_FINISH_PENDING,
    CAR_FINISHED,
    CAR_RETIRED
} CarState;

typedef enum {
    STR_AGGRESSIVE = 0,
    STR_RANDOM,
    STR_CAUTIOUS,
    STR_BLOCKER
} Strategy;

typedef struct {
    int id;
    int team;
    int num;
    char name[16];
    Strategy strategy;
    CarState state;
    int r, c;
    int laps;
    int dist;
    int last_cost;
    int turn_slot;
    int bonus;
    int fwd_since_line;
    int pending_bump;
    int finish_order;
    int place;
    unsigned rng;
} Car;

typedef struct {
    char text[384];
} LogEvent;

#define LOG_CAP 4096

typedef struct {
    Track track;
    int teams;
    int cars_per_team;
    int n_cars;
    int laps_to_go;
    int max_rounds;
    int delay_ms;
    int bonuses_on;
    int lap_retire_on;
    int stall_retire_on;
    int quiet;
    int seed;
    char logfile[256];
    char session[64];

    Car cars[MAX_CARS];
    int occ[MAX_ROWS][MAX_COLS];

    int round_no;
    int last_fwd_cost;
    int stall_moves;
    int finish_seq;
    int next_place;
    volatile int running;
    volatile int shutdown;
    volatile int interrupted;
    volatile int ready_count;
    volatile int creator_alive;

    LogEvent logq[LOG_CAP];
    int log_head, log_tail, log_count;

#if IPC_KIND == 1
    sem_t sem_board;
    sem_t sem_logmu;
    sem_t sem_turn[MAX_CARS];
    sem_t sem_done;
    sem_t sem_logq;
    sem_t sem_ready;
#endif
} Game;

typedef struct {
    Cell path[MAX_PATH];
    int path_len;
    int cost;
    int end_r, end_c;
    int laps_after;
    int dist_after;
    int lane_after;
    int finished;
    int bump_id;
} Move;

int game_init_state(Game *g);
void rebuild_occ(Game *g);
void emit_event(Game *g, const char *fmt, ...);
void print_board(Game *g);
void write_all(int fd, const char *buf, size_t n);
void delay_ms(int ms);

void do_car_turn(Game *g, int car_id);
void judge_loop(Game *g);
void car_loop(Game *g, int car_id);
void logger_loop(Game *g);

void game_request_stop(Game *g, int interrupted);
int racing_left(const Game *g);

extern Game *g_game;

#endif

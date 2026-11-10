#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


#if IPC_KIND == 3
#include <mqueue.h>
#endif

static char g_session[64];
static int g_n_cars;
static int g_shm_fd = -1;
static Game *g_map;

#if IPC_KIND != 1
static sem_t *sem_board = SEM_FAILED;
static sem_t *sem_logmu = SEM_FAILED;
static sem_t *sem_turn[MAX_CARS];
static sem_t *sem_done = SEM_FAILED;
static sem_t *sem_logq = SEM_FAILED;
static sem_t *sem_ready = SEM_FAILED;
static char n_board[64], n_logmu[64], n_done[64], n_logq[64], n_ready[64];
static char n_turn[MAX_CARS][64];
#endif

#if IPC_KIND == 3
static mqd_t mq_log = (mqd_t)-1;
static mqd_t mq_done = (mqd_t)-1;
static mqd_t mq_car[MAX_CARS];
static char n_mq_log[64], n_mq_done[64], n_mq_car[MAX_CARS][64];

typedef struct {
    int shutdown;
    int n_cars;
    int last_fwd_cost;
    int stall_moves;
    int finish_seq;
    int round_no;
    int running;
    Car cars[MAX_CARS];
} WireState;
#endif

#if IPC_KIND != 1
static void mkname(char *dst, size_t cap, const char *sess, const char *tag)
{
    snprintf(dst, cap, "/%s_%s", sess, tag);
}
#endif

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#if IPC_KIND != 1
static int open_named_sems(const char *sess, int n_cars, int create)
{
    int i;
    int flags = create ? (O_CREAT | O_EXCL) : 0;
    unsigned mode = 0600;

    mkname(n_board, sizeof(n_board), sess, "board");
    mkname(n_logmu, sizeof(n_logmu), sess, "logmu");
    mkname(n_done, sizeof(n_done), sess, "done");
    mkname(n_logq, sizeof(n_logq), sess, "logq");
    mkname(n_ready, sizeof(n_ready), sess, "ready");
    for (i = 0; i < n_cars; i++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "t%d", i);
        mkname(n_turn[i], sizeof(n_turn[i]), sess, tag);
    }

    if (create) {
        sem_unlink(n_board);
        sem_unlink(n_logmu);
        sem_unlink(n_done);
        sem_unlink(n_logq);
        sem_unlink(n_ready);
        for (i = 0; i < n_cars; i++)
            sem_unlink(n_turn[i]);
    }

    sem_board = sem_open(n_board, flags, mode, 1);
    sem_logmu = sem_open(n_logmu, flags, mode, 1);
    sem_done = sem_open(n_done, flags, mode, 0);
    sem_logq = sem_open(n_logq, flags, mode, 0);
    sem_ready = sem_open(n_ready, flags, mode, 0);
    if (sem_board == SEM_FAILED || sem_logmu == SEM_FAILED ||
        sem_done == SEM_FAILED || sem_logq == SEM_FAILED ||
        sem_ready == SEM_FAILED)
        return -1;
    for (i = 0; i < n_cars; i++) {
        sem_turn[i] = sem_open(n_turn[i], flags, mode, 0);
        if (sem_turn[i] == SEM_FAILED)
            return -1;
    }
    return 0;
}

static void close_named_sems(int n_cars, int unlink_all)
{
    int i;
    if (sem_board && sem_board != SEM_FAILED)
        sem_close(sem_board);
    if (sem_logmu && sem_logmu != SEM_FAILED)
        sem_close(sem_logmu);
    if (sem_done && sem_done != SEM_FAILED)
        sem_close(sem_done);
    if (sem_logq && sem_logq != SEM_FAILED)
        sem_close(sem_logq);
    if (sem_ready && sem_ready != SEM_FAILED)
        sem_close(sem_ready);
    for (i = 0; i < n_cars; i++) {
        if (sem_turn[i] && sem_turn[i] != SEM_FAILED)
            sem_close(sem_turn[i]);
    }
    if (unlink_all) {
        sem_unlink(n_board);
        sem_unlink(n_logmu);
        sem_unlink(n_done);
        sem_unlink(n_logq);
        sem_unlink(n_ready);
        for (i = 0; i < n_cars; i++)
            sem_unlink(n_turn[i]);
    }
}
#endif

#if IPC_KIND != 3
static int shm_name(char *dst, size_t cap, const char *sess)
{
    return snprintf(dst, cap, "/%s_shm", sess);
}

int ipc_create(Game **out, const Options *o)
{
    char name[80];
    Game *g;
    int fd;

    strncpy(g_session, o->session, sizeof(g_session) - 1);
    shm_name(name, sizeof(name), o->session);
    shm_unlink(name);
    fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    if (ftruncate(fd, (off_t)sizeof(Game)) != 0) {
        close(fd);
        shm_unlink(name);
        return -1;
    }
    g = mmap(NULL, sizeof(Game), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g == MAP_FAILED) {
        close(fd);
        shm_unlink(name);
        return -1;
    }
    memset(g, 0, sizeof(*g));
    options_apply(g, o);
    if (game_init_state(g) != 0) {
        munmap(g, sizeof(Game));
        close(fd);
        shm_unlink(name);
        return -1;
    }
    g_n_cars = g->n_cars;
    g_shm_fd = fd;
    g_map = g;

#if IPC_KIND == 1
    if (sem_init(&g->sem_board, 1, 1) != 0 ||
        sem_init(&g->sem_logmu, 1, 1) != 0 ||
        sem_init(&g->sem_done, 1, 0) != 0 ||
        sem_init(&g->sem_logq, 1, 0) != 0 ||
        sem_init(&g->sem_ready, 1, 0) != 0)
        return -1;
    {
        int i;
        for (i = 0; i < g->n_cars; i++) {
            if (sem_init(&g->sem_turn[i], 1, 0) != 0)
                return -1;
        }
    }
#else
    if (open_named_sems(o->session, g->n_cars, 1) != 0)
        return -1;
#endif
    *out = g;
    return 0;
}

int ipc_attach(Game **out, const Options *o, int timeout_ms)
{
    char name[80];
    int fd = -1;
    int waited = 0;
    Game *g;

    strncpy(g_session, o->session, sizeof(g_session) - 1);
    shm_name(name, sizeof(name), o->session);
    while (waited <= timeout_ms) {
        fd = shm_open(name, O_RDWR, 0600);
        if (fd >= 0)
            break;
        sleep_ms(50);
        waited += 50;
    }
    if (fd < 0)
        return -1;
    g = mmap(NULL, sizeof(Game), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g == MAP_FAILED) {
        close(fd);
        return -1;
    }
    g_n_cars = g->n_cars;
    g_shm_fd = fd;
    g_map = g;
#if IPC_KIND == 2
    if (open_named_sems(o->session, g->n_cars, 0) != 0)
        return -1;
#endif
    *out = g;
    return 0;
}

void ipc_cleanup(Game *g, int creator)
{
    char name[80];
    int n = g_n_cars;
    (void)g;
#if IPC_KIND == 1
    if (creator && g) {
        int i;
        sem_destroy(&g->sem_board);
        sem_destroy(&g->sem_logmu);
        sem_destroy(&g->sem_done);
        sem_destroy(&g->sem_logq);
        sem_destroy(&g->sem_ready);
        for (i = 0; i < n; i++)
            sem_destroy(&g->sem_turn[i]);
    }
#else
    close_named_sems(n, creator);
#endif
    if (g_map) {
        munmap(g_map, sizeof(Game));
        g_map = NULL;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
    if (creator) {
        shm_name(name, sizeof(name), g_session);
        shm_unlink(name);
    }
}

#else /* IPC_KIND == 3 : нет игровой shm, только семафоры и очереди */

int ipc_create(Game **out, const Options *o)
{
    static Game local;
    memset(&local, 0, sizeof(local));
    options_apply(&local, o);
    if (game_init_state(&local) != 0)
        return -1;
    strncpy(g_session, o->session, sizeof(g_session) - 1);
    g_n_cars = local.n_cars;
    if (open_named_sems(o->session, local.n_cars, 1) != 0)
        return -1;
    if (mq_init_create(o) != 0)
        return -1;
    *out = &local;
    return 0;
}

int ipc_attach(Game **out, const Options *o, int timeout_ms)
{
    static Game local;
    memset(&local, 0, sizeof(local));
    options_apply(&local, o);
    if (game_init_state(&local) != 0)
        return -1;
    strncpy(g_session, o->session, sizeof(g_session) - 1);
    g_n_cars = local.n_cars;
    {
        int waited = 0;
        while (waited <= timeout_ms) {
            if (open_named_sems(o->session, local.n_cars, 0) == 0)
                break;
            sleep_ms(50);
            waited += 50;
        }
        if (waited > timeout_ms)
            return -1;
    }
    if (mq_init_attach(o, timeout_ms) != 0)
        return -1;
    *out = &local;
    return 0;
}

void ipc_cleanup(Game *g, int creator)
{
    (void)g;
    mq_cleanup(creator, g_n_cars);
    close_named_sems(g_n_cars, creator);
}
#endif

static int sem_wait_intr(sem_t *s)
{
    int r;
    do {
        r = sem_wait(s);
    } while (r != 0 && errno == EINTR);
    return r;
}

/* sem_timedwait нет в Darwin; опрос trywait с дедлайном по CLOCK_REALTIME. */
static int sem_wait_until(sem_t *s, const struct timespec *deadline)
{
    for (;;) {
        int r = sem_trywait(s);
        if (r == 0)
            return 0;
        if (errno != EAGAIN && errno != EINTR)
            return -1;
        {
            struct timespec now;
            if (clock_gettime(CLOCK_REALTIME, &now) != 0)
                return -1;
            if (now.tv_sec > deadline->tv_sec ||
                (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec)) {
                errno = ETIMEDOUT;
                return -1;
            }
        }
        {
            struct timespec sl;
            sl.tv_sec = 0;
            sl.tv_nsec = 20L * 1000L * 1000L;
            nanosleep(&sl, NULL);
        }
    }
}

static int deadline_from_now(struct timespec *ts, int timeout_ms)
{
    if (clock_gettime(CLOCK_REALTIME, ts) != 0)
        return -1;
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
    return 0;
}

void board_lock(Game *g)
{
#if IPC_KIND == 1
    sem_wait_intr(&g->sem_board);
#else
    (void)g;
    sem_wait_intr(sem_board);
#endif
}

void board_unlock(Game *g)
{
#if IPC_KIND == 1
    sem_post(&g->sem_board);
#else
    (void)g;
    sem_post(sem_board);
#endif
}

void log_lock(Game *g)
{
#if IPC_KIND == 1
    sem_wait_intr(&g->sem_logmu);
#else
    (void)g;
    sem_wait_intr(sem_logmu);
#endif
}

void log_unlock(Game *g)
{
#if IPC_KIND == 1
    sem_post(&g->sem_logmu);
#else
    (void)g;
    sem_post(sem_logmu);
#endif
}

void car_turn_wait(Game *g, int car_id)
{
#if IPC_KIND == 1
    sem_wait_intr(&g->sem_turn[car_id]);
#else
    (void)g;
    sem_wait_intr(sem_turn[car_id]);
#endif
}

void car_turn_post(Game *g, int car_id)
{
#if IPC_KIND == 1
    sem_post(&g->sem_turn[car_id]);
#else
    (void)g;
    sem_post(sem_turn[car_id]);
#endif
}

void done_wait(Game *g)
{
#if IPC_KIND == 1
    sem_wait_intr(&g->sem_done);
#else
    (void)g;
    sem_wait_intr(sem_done);
#endif
}

void done_post(Game *g)
{
#if IPC_KIND == 1
    sem_post(&g->sem_done);
#else
    (void)g;
    sem_post(sem_done);
#endif
}

void log_item_wait(Game *g)
{
#if IPC_KIND == 1
    sem_wait_intr(&g->sem_logq);
#else
    (void)g;
    sem_wait_intr(sem_logq);
#endif
}

void log_item_post(Game *g)
{
#if IPC_KIND == 1
    sem_post(&g->sem_logq);
#else
    (void)g;
    sem_post(sem_logq);
#endif
}

void ready_post(Game *g)
{
#if IPC_KIND == 1
    sem_post(&g->sem_ready);
#else
    (void)g;
    sem_post(sem_ready);
#endif
}

void ready_wait_n(Game *g, int n)
{
    int i;
    for (i = 0; i < n; i++) {
#if IPC_KIND == 1
        sem_wait_intr(&g->sem_ready);
#else
        (void)g;
        sem_wait_intr(sem_ready);
#endif
    }
}

int ready_wait_n_timeout(Game *g, int n, int timeout_ms)
{
    struct timespec dl;
    int i;
    if (deadline_from_now(&dl, timeout_ms) != 0)
        return -1;
    for (i = 0; i < n; i++) {
        int r;
#if IPC_KIND == 1
        r = sem_wait_until(&g->sem_ready, &dl);
#else
        (void)g;
        r = sem_wait_until(sem_ready, &dl);
#endif
        if (r != 0)
            return -1;
    }
    return 0;
}

void wake_all_for_shutdown(Game *g)
{
    int i;
    g->shutdown = 1;
#if IPC_KIND == 3
    for (i = 0; i < g->n_cars; i++) {
        mq_send_turn(i, g);
        car_turn_post(g, i);
    }
    mq_send_log("");
    log_item_post(g);
    done_post(g);
#else
    for (i = 0; i < g->n_cars; i++)
        car_turn_post(g, i);
    done_post(g);
    log_item_post(g);
#endif
}

#if IPC_KIND == 3
static void pack_wire(const Game *g, WireState *w)
{
    memset(w, 0, sizeof(*w));
    w->shutdown = g->shutdown;
    w->n_cars = g->n_cars;
    w->last_fwd_cost = g->last_fwd_cost;
    w->stall_moves = g->stall_moves;
    w->finish_seq = g->finish_seq;
    w->round_no = g->round_no;
    w->running = g->running;
    memcpy(w->cars, g->cars, sizeof(g->cars));
}

static void unpack_wire(Game *g, const WireState *w)
{
    g->shutdown = w->shutdown;
    g->n_cars = w->n_cars;
    g->last_fwd_cost = w->last_fwd_cost;
    g->stall_moves = w->stall_moves;
    g->finish_seq = w->finish_seq;
    g->round_no = w->round_no;
    g->running = w->running;
    memcpy(g->cars, w->cars, sizeof(g->cars));
    rebuild_occ(g);
}

int mq_init_create(const Options *o)
{
    struct mq_attr attr;
    int i;
    int n = o->teams * o->cars_per_team;

    for (i = 0; i < MAX_CARS; i++)
        mq_car[i] = (mqd_t)-1;
    mq_log = (mqd_t)-1;
    mq_done = (mqd_t)-1;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 8;
    attr.mq_msgsize = (long)sizeof(WireState);
    if (attr.mq_msgsize < 400)
        attr.mq_msgsize = 400;

    mkname(n_mq_log, sizeof(n_mq_log), o->session, "mqlog");
    mkname(n_mq_done, sizeof(n_mq_done), o->session, "mqdone");
    mq_unlink(n_mq_log);
    mq_unlink(n_mq_done);
    mq_log = mq_open(n_mq_log, O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
    mq_done = mq_open(n_mq_done, O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
    if (mq_log == (mqd_t)-1 || mq_done == (mqd_t)-1)
        return -1;
    for (i = 0; i < n; i++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "mqc%d", i);
        mkname(n_mq_car[i], sizeof(n_mq_car[i]), o->session, tag);
        mq_unlink(n_mq_car[i]);
        mq_car[i] = mq_open(n_mq_car[i], O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
        if (mq_car[i] == (mqd_t)-1)
            return -1;
    }
    return 0;
}

int mq_init_attach(const Options *o, int timeout_ms)
{
    int i, waited = 0;
    int n = o->teams * o->cars_per_team;
    mkname(n_mq_log, sizeof(n_mq_log), o->session, "mqlog");
    mkname(n_mq_done, sizeof(n_mq_done), o->session, "mqdone");
    while (waited <= timeout_ms) {
        mq_log = mq_open(n_mq_log, O_RDWR);
        mq_done = mq_open(n_mq_done, O_RDWR);
        if (mq_log != (mqd_t)-1 && mq_done != (mqd_t)-1)
            break;
        sleep_ms(50);
        waited += 50;
    }
    if (mq_log == (mqd_t)-1 || mq_done == (mqd_t)-1)
        return -1;
    for (i = 0; i < n; i++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "mqc%d", i);
        mkname(n_mq_car[i], sizeof(n_mq_car[i]), o->session, tag);
        mq_car[i] = mq_open(n_mq_car[i], O_RDWR);
        if (mq_car[i] == (mqd_t)-1)
            return -1;
    }
    return 0;
}

void mq_cleanup(int creator, int n_cars)
{
    int i;
    if (mq_log != (mqd_t)-1)
        mq_close(mq_log);
    if (mq_done != (mqd_t)-1)
        mq_close(mq_done);
    for (i = 0; i < n_cars; i++) {
        if (mq_car[i] != (mqd_t)-1)
            mq_close(mq_car[i]);
    }
    if (creator) {
        mq_unlink(n_mq_log);
        mq_unlink(n_mq_done);
        for (i = 0; i < n_cars; i++)
            mq_unlink(n_mq_car[i]);
    }
}

int mq_send_turn(int car_id, const Game *g)
{
    WireState w;
    pack_wire(g, &w);
    return mq_send(mq_car[car_id], (const char *)&w, sizeof(w), 0) == 0 ? 0 : -1;
}

int mq_recv_turn(Game *g, int car_id)
{
    WireState w;
    ssize_t n = mq_receive(mq_car[car_id], (char *)&w, sizeof(w), NULL);
    if (n != (ssize_t)sizeof(w))
        return -1;
    unpack_wire(g, &w);
    return 0;
}

int mq_send_done(const Game *g)
{
    WireState w;
    pack_wire(g, &w);
    return mq_send(mq_done, (const char *)&w, sizeof(w), 0) == 0 ? 0 : -1;
}

int mq_recv_done(Game *g)
{
    WireState w;
    ssize_t n = mq_receive(mq_done, (char *)&w, sizeof(w), NULL);
    if (n != (ssize_t)sizeof(w))
        return -1;
    unpack_wire(g, &w);
    return 0;
}

int mq_send_log(const char *text)
{
    char buf[400];
    memset(buf, 0, sizeof(buf));
    strncpy(buf, text, sizeof(buf) - 1);
    return mq_send(mq_log, buf, sizeof(buf), 0) == 0 ? 0 : -1;
}

int mq_recv_log(char *text, size_t cap)
{
    char buf[sizeof(WireState)];
    ssize_t n = mq_receive(mq_log, buf, sizeof(buf), NULL);
    if (n < 0)
        return -1;
    if (n >= (ssize_t)sizeof(buf))
        n = (ssize_t)sizeof(buf) - 1;
    buf[n] = '\0';
    strncpy(text, buf, cap - 1);
    text[cap - 1] = '\0';
    return 0;
}
#endif

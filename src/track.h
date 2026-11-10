#ifndef TRACK_H
#define TRACK_H


#define MAX_ROWS 40
#define MAX_COLS 80
#define MAX_LANES 8
#define MAX_LANE_LEN 256
#define MAX_PATH 40
#define MAX_CARS 16
#define MAX_TEAMS 8

typedef struct {
    int r;
    int c;
} Cell;

typedef struct {
    int valid;
    int lane;
    int idx;
    int fin;
    Cell fwd;
    Cell back;
    Cell inward;
    Cell outward;
} Node;

typedef struct {
    int rows;
    int cols;
    int lanes;
    int lane_len[MAX_LANES];
    Cell lane_cell[MAX_LANES][MAX_LANE_LEN];
    Node map[MAX_ROWS][MAX_COLS];
} Track;

int track_init(Track *t, int rows, int cols, int lanes);
int track_is_cell(const Track *t, int r, int c);
int track_lane(const Track *t, int r, int c);
Node *track_node(Track *t, int r, int c);
const Node *track_node_c(const Track *t, int r, int c);

#endif

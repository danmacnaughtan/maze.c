/* gcc -o maze maze.c -lm -lncurses */
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ncurses.h>

#define COLOR_GRAY 6
#define COLOR_GRAY2 5

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum { MAXCELLS = 65536, /* 256x256 */ };

typedef struct { int x, y; } coord;
typedef struct { int x, y, w, h; } rect;
typedef struct { char obs, idx; } cell_t;
typedef struct { int w, h; cell_t cells[MAXCELLS]; } map_t;

struct {
    int sw, sh; /* screen width/height */
    coord plr;  /* player coordinates */
    bool quit;  /* should exit game */
    rect vp;    /* viewport */
    map_t map;  /* the game map */
    bool win;
    int moves;
} gamestate = { 0 };

#define GS gamestate

/* seed rand() with the current time or given seed */
void rndseed(time_t seed) {
    srand(seed ? seed : time(NULL));
#ifdef DEBUG
    printf("rndseed=%ld\n", seed);
#endif
}

/* random integer in range (MIN, MAX) */
int rndr(int min, int max) {
    int temp;
    if (max < min) (temp = max, max = min, min = temp);
    return min + rand() % (max + 1 - min);
}

void map_resize(map_t *map, int w, int h) {
    assert(w*h <= MAXCELLS);
    map->w = w, map->h = h;
}

bool map_inbounds(map_t *map, coord p) {
    return p.x >= 0 && p.x < map->w && p.y >= 0 && p.y < map->h;
}

cell_t map_get(map_t *map, coord p) {
    assert(p.x >= 0 && p.x < map->w && p.y >= 0 && p.y < map->h);
    return map->cells[p.y * map->w + p.x];
}

void map_set(map_t *map, coord p, cell_t cell) {
    if (map_inbounds(map, (coord){p.x, p.y}))
        map->cells[p.y * map->w + p.x] = cell;
}

rect vp_get_offset(rect vp, coord p, map_t *map) {
    /* calculate top-left visible tile */
    int sx = MAX(0, MIN((p.x - vp.w / 2), map->w - vp.w));
    int sy = MAX(0, MIN((p.y - vp.h / 2), map->h - vp.h));
    return (rect){sx, sy, vp.w, vp.h};
}

bool grect_valid(rect r) {return r.w > 0 && r.h > 0;}

/* check if a coord in rect */
bool gcoord_in_rect(rect r, coord c) {
    if (!grect_valid(r) || r.y < 0 || r.x < 0) return false;
    int max_y = r.y + r.h-1; /* inclusive top edge */
    int max_x = r.x + r.w; /* inclusive left edge */
    return c.y >= r.y && c.y <= max_y && c.x >= r.x && c.x <= max_x;
}

/* shuffle an array of integers of size n */
void gshufflei(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rndr(0, i);
        int temp = arr[i];
        arr[i] = arr[j], arr[j] = temp;
    }
}

/* coord stack */
typedef struct { coord d[MAXCELLS]; int top; } stack_t;
void stack_push(stack_t *stack, coord p) {stack->d[stack->top++] = p;}
coord stack_peek(stack_t *stack) {return stack->d[stack->top-1];}
coord stack_pop(stack_t *stack) {return stack->d[--stack->top];}
bool stack_empty(stack_t *stack) {return stack->top == 0;}

/* clear a straight path between cells */
void maze_path(map_t *map, coord a, coord b) {
    int x1=a.x, y1=a.y, x2=b.x, y2=b.y;
    if (x1 == x2) { /* horizontal */
        int miny = MIN(y1, y2), maxy = MAX(y1, y2);
        for (int y=miny; y<=maxy; y++)
            map_set(map, (coord){x1, y}, (cell_t){.obs=0, .idx='.'});
    } else if (y1 == y2) { /* vertical */
        int minx = MIN(x1, x2), maxx = MAX(x1, x2);
        for (int x=minx; x<=maxx; x++)
            map_set(map, (coord){x, y1}, (cell_t){.obs=0, .idx='.'});
    }
}

/* maze exploration phase of generation */
bool maze_explore(coord p, bool *visited, stack_t *stack, map_t *map) {
    /* define our direction coordinates */
    coord dirs[4] = {{p.x-2,p.y}, {p.x+2,p.y}, {p.x,p.y-2}, {p.x,p.y+2}};
    int idx[4] = {0, 1, 2, 3}; /* direction index */
    gshufflei(idx, 4); /* randomize the direction order */
    bool found_exit = false;
    /* explore cardinal directions */
    for (int i = 0; i < 4; i++) {
        coord np = dirs[idx[i]];
        if (!map_inbounds(map, np) || visited[np.y * map->w + np.x])
            continue;
        maze_path(map, p, np);
        visited[np.y * map->w + np.x] = true;
        stack_push(stack, np);
        found_exit = true;
        /* tune number of junctions */
        if (rndr(0, 1)) break;
    }
    return found_exit;
}

/* maze generation using DFS backtracking - return start postion */
coord maze_init(map_t *map) {
    /* set all cells to walls */
    for (int i = 0; i < MAXCELLS; i++)
        map->cells[i] = (cell_t){.idx='#', .obs=1};
    /* define the visited array and stack */
    bool visited[MAXCELLS] = {false};
    stack_t stack = {0};
    /* start with random start coord */
    coord start = {rndr(0, map->w-1), rndr(0, map->h-1)};
    map_set(map, start, (cell_t){0, '.'});
    stack_push(&stack, start);
    visited[start.y * map->w + start.x] = true;
    /* start carving */
    while (!stack_empty(&stack)) {
        coord current = stack_peek(&stack);
        if (maze_explore(current, visited, &stack, map))
            continue; /* keep going deeper */
        (void)stack_pop(&stack); /* backtrack if stuck? */
    }
    return start;
}

/* place our goal somewhere in the map */
void gplace_stairs(void) {
    for (;;) {
        coord p = {rndr(0, GS.map.w-1), rndr(0, GS.map.h-1)};
        if (map_get(&GS.map, p).obs != 0) continue;
        map_set(&GS.map, p, (cell_t){.obs=0, .idx='%'});
        break;
    }
}

/* check line of sight between two points */
bool fov_check(map_t *map, coord p1, coord p2) {
    int dx = p2.x - p1.x; /* delta x */
    int dy = p2.y - p1.y; /* delta y */
    /* calculate the ray steps from p1 to p2 cell */
    int abs_dx = abs(dx);
    int abs_dy = abs(dy);
    int step = abs_dx >= abs_dy ? abs_dx : abs_dy;
    dx = (dx > 0) ? 1 : dx == 0 ? 0 : -1;
    dy = (dy > 0) ? 1 : dy == 0 ? 0 : -1;
    coord p = p1;
    for (int i = 0; i < step; ++i) {
        p.x += dx;
        p.y += dy;
        if (!map_inbounds(map, p)) return false;
        if (p.x == p2.x && p.y == p2.y) return true;
        if (map_get(map, p).obs != 0) return false;
    }
    return false;
}

void gmove(coord step) {
    coord new = GS.plr;
    new.y += step.y;
    new.x += step.x;
    /* make sure in bounds */
    if (!map_inbounds(&GS.map, new)) return;
    /* check if there is an obsticle */
    if (map_get(&GS.map, new).obs) return;
    /* check if the player found exit */
    if (map_get(&GS.map, new).idx == '%')
        GS.win = GS.quit = true;
    GS.plr = new;
    GS.moves++;
}

/* handle input keys */
void ginput(char key) {
    coord step = {0, 0};
    switch (key) {
        case 'q': GS.quit = true; break;
        case 'w': step.y--; break;
        case 's': step.y++; break;
        case 'a': step.x--; break;
        case 'd': step.x++; break;
    }
    gmove(step);
}

/* draw a line to the screen */
void gdraw_line(coord a, coord b, char ch) {
    float dx = b.x - a.x; /* Bresenham's algorithm */
    float dy = b.y - a.y;
    /* handle the case when line is too short (adjacent dots only) */
    /* don't draw very long diagonal lines that might clip */
    if ((dx < -9 || dx > 9) && (dy < -9 || dy > 9)) return;     
    float steps = (fabs(dx) > fabs(dy)) ? fabs(dx) : fabs(dy);
    for (int i = 0; i <= steps; i++) {
        float fx = (uint)(i * dx / steps + a.x);
        float fy = (uint)(i * dy / steps + a.y);
        /* bounds check */
        if (!(fx >= 0 && fx < GS.sw && fy >= 0 && fy < GS.sh)) continue;
        mvaddch(fy, fx, ch);
    }
}

/* draw a rect to the screen */
void gdraw_rect(rect r, char ch, bool fill) {
    /* bounds check */
    if (r.y<0||r.x<0||r.y>=GS.sh-1||r.x>=GS.sw-1||r.h<=0||r.w<=0) return;
    /* draw the edges */
    gdraw_line((coord){r.x, r.y}, (coord){r.x+r.w-1, r.y}, ch);//top
    gdraw_line((coord){r.x, r.y+r.h-1}, (coord){r.x+r.w-1, r.y+r.h-1}, ch);//bot
    gdraw_line((coord){r.x, r.y}, (coord){r.x, r.y+r.h-1}, ch);//left
    gdraw_line((coord){r.x+r.w-1, r.y}, (coord){r.x+r.w-1, r.y+r.h-1}, ch);//right
    /* handle fill */
    if (!(fill && r.h > 2 && r.w > 1)) return;
    for (int y = r.y+1; y < r.y+r.h - 1; y++)
        gdraw_line((coord){r.x+1, y}, (coord){r.x+r.w-1, y}, ch);
}

/* convenience function for identifying cells with visible neighbors
 * - stretches player visibility a little bit */
bool gvisible_neighbors(map_t *map, coord p, bool *infov) {
    if (infov[p.y * map->w + p.x]) return true;
    coord dirs[8] = {
        {p.x-1,p.y-1}, {p.x,p.y-1}, {p.x+1,p.y-1}, {p.x+1,p.y},
        {p.x+1,p.y+1}, {p.x,p.y+1}, {p.x-1,p.y+1}, {p.x-1,p.y},
    };
    for (int i = 0; i < 8; ++i) {
        coord p = dirs[i];
        if (map_inbounds(map, p) && map_get(map, p).obs == 0 && infov[p.y * map->w + p.x])
            return true;
    }
    return false;
}

/* draw the game screen */
void gdraw(void) {
    /* border around viewport */
    rect border = {GS.vp.x-1, GS.vp.y-1, GS.vp.w+2, GS.vp.h+2};
    /* draw borders */
    attron(COLOR_PAIR(3));
    gdraw_rect(border, '#', false);
    attroff(COLOR_PAIR(3));
    /* first pass - determine visibility */
    rect offset = vp_get_offset(GS.vp, GS.plr, &GS.map);
    bool infov[MAXCELLS] = {false};
    for (int y = 0; y < GS.vp.h; ++y) {
        for (int x = 0; x < GS.vp.w; ++x) {
            coord p = {offset.x + x, offset.y + y};
            if (!map_inbounds(&GS.map, p)) continue;
            if (!fov_check(&GS.map, GS.plr, p)) continue;
            infov[p.y * GS.map.w + p.x] = true;
        }
    }
    /* draw visible cells in viewport */
    for (int y = 0; y < GS.vp.h; ++y) {
        for (int x = 0; x < GS.vp.w; ++x) {
            coord p = {offset.x + x, offset.y + y};
            if (!map_inbounds(&GS.map, p)) continue;
            if (!gvisible_neighbors(&GS.map, p, infov)) {
                mvaddch(GS.vp.y + y, GS.vp.x + x, ' ');
                continue;
            }
            cell_t cell = map_get(&GS.map, p);
            if (cell.idx) {
                if (cell.idx == '.') attron(COLOR_PAIR(4));
                if (cell.idx == '#') attron(COLOR_PAIR(5));
                if (cell.idx == '%') attron(COLOR_PAIR(6));
                mvaddch(GS.vp.y + y, GS.vp.x + x, cell.idx);
                if (cell.idx == '.') attroff(COLOR_PAIR(4));
                if (cell.idx == '#') attroff(COLOR_PAIR(5));
                if (cell.idx == '%') attroff(COLOR_PAIR(6));
            } else {
                mvaddch(GS.vp.y + y, GS.vp.x + x, ' ');
            }
        }
    }
    /* draw player */
    int px = GS.plr.x - offset.x + GS.vp.x;
    int py = GS.plr.y - offset.y + GS.vp.y;
    if (gcoord_in_rect(GS.vp, (coord){px, py})) {
        attron(A_BOLD);
        mvaddch(py, px, '@');
        attroff(A_BOLD);
    }
    refresh(); /* redraw screen */
}

/* setup ncurses */
void gsetup_ncurses(void) {
    initscr();     /* initialize curses screen */
    cbreak();      /* disable line buffering */
    noecho();      /* don't echo keystrokes */
    curs_set(0);   /* disable cursor */
    start_color(); /* enable color support */
    keypad(stdscr, TRUE);   /* enable keypad for special keys */
    nodelay(stdscr, FALSE); /* block mode - wait for input */
}

/* configure ncurses colors */
void ginit_colors(void) {
    init_color(COLOR_BLACK, 0, 0, 0);
    init_color(COLOR_GRAY, 300, 300, 300);
    init_color(COLOR_GRAY2, 700, 700, 700);
    init_pair(1, COLOR_WHITE, COLOR_BLACK); // DEFAULT
    init_pair(2, COLOR_BLACK, COLOR_WHITE); // INVERSE
    init_pair(3, COLOR_GRAY, COLOR_GRAY);   // SOLID_GRAY
    init_pair(4, COLOR_GRAY, COLOR_BLACK);  // FADED_GRAY
    init_pair(5, COLOR_GRAY2, COLOR_BLACK); // LIGHTER_GRAY
    init_pair(6, COLOR_GREEN, COLOR_BLACK); // GREEN CHAR
}

/* setup game and libs */
void gsetup(void) {
    rndseed(0);
    gsetup_ncurses();
    getmaxyx(stdscr, GS.sh, GS.sw);
    GS.vp = (rect){1, 1, 16, 16};
    /* difficulty easy=16x16, normal=32x32, hard=64x64, nightmare=128x128 */
    map_resize(&GS.map, 64, 64);
    GS.plr = maze_init(&GS.map);
    gplace_stairs();
    ginit_colors();
}

/* cleanup game and libs */
void gcleanup(void) {
    endwin(); /* end curses and restore screen */
}

int main(void) {
    gsetup(), gdraw();
    while (!GS.quit) ginput(getch()), gdraw();
    gcleanup();
    if (GS.win) printf("Congratulations! You escaped the maze in %d moves!\n", GS.moves);
    else printf("You remain lost in the maze... game over.\n");
    return 0;
}

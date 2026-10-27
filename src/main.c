#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscalls/proc.h>
#include <syscalls/filesystem.h>
#include <syscalls/syscall_generic.h>


/*
 * Common fallback key codes.  If your environment provides KEY_* defines,
 * they will override these because of #ifndef.
 */
#ifndef KEY_UP
#define KEY_UP 103
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 108
#endif
#ifndef KEY_LEFT
#define KEY_LEFT 105
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT 106
#endif

#ifndef KEY_W
#define KEY_W 17
#endif
#ifndef KEY_S
#define KEY_S 31
#endif
#ifndef KEY_A
#define KEY_A 30
#endif
#ifndef KEY_D
#define KEY_D 32
#endif

//things not defined in libc
#define SIZE_MAX ((size_t)-1)

typedef struct {
    uint64_t seconds;
    uint32_t nanoseconds;
} Duration;

typedef struct {
    Duration generated_at;
    uint32_t code;
    uint32_t value;
} InputEvent;

#define STEP_NS 100000000ULL

enum {
    STATE_EMPTY = 0,
    STATE_BODY  = 1,
    STATE_HEAD  = 2,
    STATE_FOOD  = 3
};

enum {
    DIR_UP    = 1,
    DIR_DOWN  = 2,
    DIR_LEFT  = 3,
    DIR_RIGHT = 4
};

static FILE *fb_file = NULL;
static int input_fd = -1;
static FILE *tty_file = NULL;

static uint32_t fb_w = 0;
static uint32_t fb_h = 0;
static uint64_t row_bytes = 0;

static uint32_t cell_size = 0;
static uint32_t grid_w = 0;
static uint32_t grid_h = 0;
static uint32_t grid_cells = 0;

static uint8_t *cell_state = 0;
static uint32_t *snake = 0;

static uint32_t head_idx = 0;
static uint32_t tail_idx = 0;
static uint32_t snake_len = 0;
static uint32_t food_idx = 0;

static int dir = DIR_RIGHT;
static int next_dir = DIR_RIGHT;

static uint8_t *fill[4] = { 0, 0, 0, 0 };
static uint32_t rng_state = 0x9e3779b9U;

static void fail(void) {
    exit(1);
}

static void write_tty(const char *msg, uint64_t len) {

    while (len > 0) {
        size_t chunk;
        if (len > (uint64_t)SIZE_MAX) {
            chunk = (size_t)SIZE_MAX;
        } else {
            chunk = (size_t)len;
        }

        size_t w = fwrite(msg, 1, chunk, tty_file);
        if (w <= 0) {
            break;
        }

        msg += (size_t)w;
        len -= (uint64_t)w;
    }
}

static void game_over(int win) {
    if (win) {
        write_tty("win\n", sizeof("win\n") - 1);
    } else {
        write_tty("lost\n", sizeof("lost\n") - 1);
    }

    exit(0);
}

static int write_at(FILE *file, uint64_t offset, const uint8_t *buf, uint64_t len) {
    if (len == 0) {
        return 1;
    }

    if (len > (uint64_t)SIZE_MAX) {
        return 0;
    }

    if (offset > (uint64_t)0x7fffffffffffffffULL) {
        return 0;
    }

    if (fseek(file, offset, SEEK_SET) < 0) {
        return 0;
    }

    const uint8_t *p = buf;
    uint64_t rem = len;

    while (rem > 0) {
        size_t w = fwrite(p, 1, (size_t)rem, file);
        if (w <= 0) {
            return 0;
        }

        p += (uint64_t)w;
        rem -= (uint64_t)w;
    }

    return 1;
}

static uint32_t choose_cell_size(uint64_t w, uint64_t h) {
    static const uint32_t candidates[] = { 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
    static const uint32_t MIN_GRID_SIZE = 50;

    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        uint32_t cs = candidates[i];

        uint64_t gw = w / cs;
        uint64_t gh = h / cs;

        if (gw < MIN_GRID_SIZE || gh < MIN_GRID_SIZE) {
            continue;
        }

        if (gw > 0x7FFFFFFFU || gh > 0x7FFFFFFFU) {
            continue;
        }

        if (gw != 0 && gh > 0x7FFFFFFFU / gw) {
            continue;
        }

        return cs;
    }

    return 0;
}

static void clear_screen(void) {
    if (row_bytes == 0) {
        fail();
    }

    if (fb_h > UINT64_MAX / row_bytes) {
        fail();
    }

    uint64_t total = fb_h * row_bytes;
    if (total == 0) {
        fail();
    }

    size_t chunk = 1024U * 1024U;
    if ((uint64_t)chunk > total) {
        chunk = (size_t)total;
    }

    uint8_t *buf = malloc(chunk);
    if (!buf) {
        fail();
    }

    for (size_t i = 0; i < chunk; ++i) {
        buf[i] = 0;
    }

    uint64_t pos = 0;
    while (pos < total) {
        uint64_t rem = total - pos;
        size_t len = ((uint64_t)chunk > rem) ? (size_t)rem : chunk;

        if (!write_at(fb_file, pos, buf, len)) {
            fail();
        }

        pos += (uint64_t)len;
    }

    free(buf);
}

static void init_fill(void) {
    static const uint8_t color[4][3] = {
        {   0,   0,   0 }, /* empty / background */
        {   0, 180,   0 }, /* snake body */
        {   0, 255,   0 }, /* snake head */
        { 255,  80,  80 }  /* food */
    };

    if (cell_size == 0) {
        fail();
    }

    uint64_t line_len64 = (uint64_t)cell_size * 3;
    if (line_len64 > (uint64_t)SIZE_MAX) {
        fail();
    }

    size_t line_len = (size_t)line_len64;

    for (int s = 0; s < 4; ++s) {
        fill[s] = malloc(line_len);
        if (!fill[s]) {
            fail();
        }

        for (uint32_t i = 0; i < cell_size; ++i) {
            fill[s][i * 3 + 0] = color[s][0];
            fill[s][i * 3 + 1] = color[s][1];
            fill[s][i * 3 + 2] = color[s][2];
        }
    }
}

static void draw_cell(uint32_t idx, uint8_t state) {
    if (idx >= grid_cells) {
        fail();
    }

    if (grid_w == 0) {
        fail();
    }

    if (state > STATE_FOOD) {
        state = STATE_EMPTY;
    }

    uint32_t x = idx % grid_w;
    uint32_t y = idx / grid_w;

    uint64_t base =
        (uint64_t)y * cell_size * row_bytes +
        (uint64_t)x * cell_size * 3;

    uint64_t line_len = (uint64_t)cell_size * 3;
    const uint8_t *src = fill[state];

    for (uint32_t r = 0; r < cell_size; ++r) {
        uint64_t off = base + (uint64_t)r * row_bytes;
        if (!write_at(fb_file, off, src, line_len)) {
            fail();
        }
    }
}

static void set_cell(uint32_t idx, uint8_t state) {
    if (idx >= grid_cells) {
        fail();
    }

    if (cell_state[idx] == state) {
        return;
    }

    cell_state[idx] = state;
    draw_cell(idx, state);
}

static uint32_t next_rand(void) {
    uint32_t x = rng_state;

    if (x == 0) {
        x = 0x9e3779b9U;
    }

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    rng_state = x;
    return x;
}

static void spawn_food(void) {
    if (grid_cells == 0) {
        fail();
    }

    if (snake_len >= grid_cells) {
        game_over(1);
        return;
    }

    uint32_t start = next_rand() % grid_cells;
    uint32_t idx = start;

    for (uint32_t k = 0; k < grid_cells; ++k) {
        if (cell_state[idx] == STATE_EMPTY) {
            food_idx = idx;
            set_cell(idx, STATE_FOOD);
            return;
        }

        ++idx;
        if (idx == grid_cells) {
            idx = 0;
        }
    }

    game_over(1);
}

static void init_snake(void) {
    if (grid_w < 3 || grid_h < 3) {
        fail();
    }

    uint32_t cx = grid_w / 2;
    uint32_t cy = grid_h / 2;

    if (cy == 0 || cy + 1 >= grid_h) {
        fail();
    }

    /*
     * Start with a vertical snake:
     *   tail
     *   body
     *   head -> moving right
     */
    uint32_t y_tail = cy - 1;
    uint32_t y_body = cy;
    uint32_t y_head = cy + 1;

    uint32_t p_tail = (uint32_t)((uint64_t)y_tail * grid_w + cx);
    uint32_t p_body = (uint32_t)((uint64_t)y_body * grid_w + cx);
    uint32_t p_head = (uint32_t)((uint64_t)y_head * grid_w + cx);

    snake[0] = p_tail;
    snake[1] = p_body;
    snake[2] = p_head;

    tail_idx = 0;
    head_idx = 2;
    snake_len = 3;

    set_cell(p_tail, STATE_BODY);
    set_cell(p_body, STATE_BODY);
    set_cell(p_head, STATE_HEAD);

    dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
}

static void process_events(void) {
    if (input_fd < 0) {
        return;
    }

    for (;;) {
        InputEvent ev;
        syscall_2ret ret = _fread(input_fd, sizeof(ev), &ev, 1);
        int bytes_read = ret.ret0;

        /*
         * Non-blocking input may return 0 or -1 when no event is available.
         * Do not exit in that case.
         */
        if (bytes_read != sizeof(ev)) {
            break;
        }

        /* value == 0 means released; value == 1 means pressed/repeated. */
        if (ev.value == 0) {
            continue;
        }

        int nd = -1;

        if (ev.code == KEY_UP || ev.code == KEY_W) {
            nd = DIR_UP;
        } else if (ev.code == KEY_DOWN || ev.code == KEY_S) {
            nd = DIR_DOWN;
        } else if (ev.code == KEY_LEFT || ev.code == KEY_A) {
            nd = DIR_LEFT;
        } else if (ev.code == KEY_RIGHT || ev.code == KEY_D) {
            nd = DIR_RIGHT;
        }

        if (nd < 0) {
            continue;
        }

        /* Prevent 180-degree reversal. */
        if ((dir == DIR_UP    && nd == DIR_DOWN) ||
            (dir == DIR_DOWN  && nd == DIR_UP) ||
            (dir == DIR_LEFT  && nd == DIR_RIGHT) ||
            (dir == DIR_RIGHT && nd == DIR_LEFT)) {
            continue;
        }

        next_dir = nd;
    }
}

static void step(void) {
    if (grid_w == 0 || grid_h == 0) {
        fail();
    }

    dir = next_dir;

    if (head_idx >= grid_cells || tail_idx >= grid_cells) {
        fail();
    }

    uint32_t head = snake[head_idx];
    uint32_t hx = head % grid_w;
    uint32_t hy = head / grid_w;

    uint32_t nx = hx;
    uint32_t ny = hy;

    if (dir == DIR_UP) {
        if (hy == 0) {
            game_over(0);
            return;
        }
        --ny;
    } else if (dir == DIR_DOWN) {
        if (hy + 1 >= grid_h) {
            game_over(0);
            return;
        }
        ++ny;
    } else if (dir == DIR_LEFT) {
        if (hx == 0) {
            game_over(0);
            return;
        }
        --nx;
    } else if (dir == DIR_RIGHT) {
        if (hx + 1 >= grid_w) {
            game_over(0);
            return;
        }
        ++nx;
    } else {
        fail();
    }

    uint32_t new_idx = (uint32_t)((uint64_t)ny * grid_w + nx);
    uint32_t old_head = head;
    uint32_t old_tail = snake[tail_idx];

    uint8_t st = cell_state[new_idx];
    int eat = 0;
    int collide = 0;

    if (st == STATE_FOOD) {
        eat = 1;
    } else if (st == STATE_HEAD) {
        collide = 1;
    } else if (st == STATE_BODY) {
        /* Moving into the old tail is allowed when not eating. */
        if (!(new_idx == old_tail && !eat)) {
            collide = 1;
        }
    }

    if (collide) {
        game_over(0);
        return;
    }

    set_cell(old_head, STATE_BODY);

    head_idx = head_idx + 1;
    if (head_idx >= grid_cells) {
        head_idx = 0;
    }

    snake[head_idx] = new_idx;
    set_cell(new_idx, STATE_HEAD);

    if (eat) {
        ++snake_len;

        if (snake_len >= grid_cells) {
            game_over(1);
            return;
        }

        spawn_food();
    } else {
        if (old_tail != new_idx) {
            set_cell(old_tail, STATE_EMPTY);
        }

        tail_idx = tail_idx + 1;
        if (tail_idx >= grid_cells) {
            tail_idx = 0;
        }
    }
}

void print_args(int argc, char **argv, FILE *tty_file) {
    fprintf(tty_file, "argc: %d\n", argc);

    //first print the argv, then pointers in argv, then strings in argv

    fprintf(tty_file, "argv table addr: %p\n", (void *)argv);

    for (int i = 0; i < argc; ++i) {
        fprintf(tty_file, "argv[%d] addr: %p\n", i, (void *)argv[i]);
    }

    for (int i = 0; i < argc; ++i) {
        fprintf(tty_file, "argv[%d]: %s\n", i, argv[i]);
    }
}

int main(int argc, char **argv) {
    tty_file = fopen("/tty", "+");
    if (!tty_file) {
        fail();
    }

    print_args(argc, argv, tty_file);

    fb_file = fopen("/fb", "+");
    if (!fb_file) {
        fail();
    }

    if (fread(&fb_w, sizeof(fb_w), 1, fb_file) != 1) {
        fail();
    }

    if (fread(&fb_h, sizeof(fb_h), 1, fb_file) != 1) {
        fail();
    }

    if (fb_w == 0 || fb_h == 0) {
        fail();
    }

    row_bytes = fb_w * 3;

    cell_size = choose_cell_size(fb_w, fb_h);
    if (cell_size == 0) {
        fail();
    }

    uint64_t gw64 = fb_w / cell_size;
    uint64_t gh64 = fb_h / cell_size;

    if (gw64 < 3 || gh64 < 3) {
        fail();
    }

    if (gw64 > 0x7FFFFFFFU || gh64 > 0x7FFFFFFFU) {
        fail();
    }

    if (gw64 != 0 && gh64 > 0x7FFFFFFFU / gw64) {
        fail();
    }

    grid_w = (uint32_t)gw64;
    grid_h = (uint32_t)gh64;
    grid_cells = (uint32_t)(grid_w * grid_h);

    if (grid_cells == 0) {
        fail();
    }

    clear_screen();
    init_fill();

    if (grid_cells > (uint32_t)(SIZE_MAX / sizeof(uint8_t))) {
        fail();
    }

    cell_state = malloc((size_t)grid_cells);
    if (!cell_state) {
        fail();
    }

    for (uint32_t i = 0; i < grid_cells; ++i) {
        cell_state[i] = STATE_EMPTY;
    }

    if (grid_cells > (uint32_t)(SIZE_MAX / sizeof(uint32_t))) {
        fail();
    }

    snake = malloc((size_t)grid_cells * sizeof(uint32_t));
    if (!snake) {
        free(cell_state);
        cell_state = 0;
        fail();
    }

    /* Seed a small xorshift RNG from time. */
    uint64_t tbuf[10];
    rng_state = 0x9e3779b9U;

    if (_time((struct TimeInfo *)tbuf) != 0) {
        fail();
    }

    rng_state ^= (uint32_t)tbuf[0];
    rng_state ^= (uint32_t)(tbuf[1] << 1);
    rng_state ^= (uint32_t)(tbuf[2] << 2);
    rng_state ^= (uint32_t)(tbuf[3] << 3);

    if (rng_state == 0) {
        rng_state = 0x9e3779b9U;
    }

    init_snake();
    spawn_food();

    char *input_path = "/input/all";

    uint64_t flags = 0b11;
    input_fd = _fopen(strlen(input_path), input_path, -1, flags, -1);
    if (input_fd < 0) {
        fail();
    }

    for (;;) {
        process_events();
        step();

        if (_sleep(0, STEP_NS) != 0) {
            fail();
        }
    }

    return 0;
}

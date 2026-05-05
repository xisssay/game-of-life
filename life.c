#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>

#define FIELD    20
#define MAX_GENS 500
#define SAVEFILE "life_configs.txt"

/* ================================================================
   Terminal utilities
   ================================================================ */
static struct termios _orig_term;
static int _raw = 0;

void restore_term(void) {
    if (!_raw) return;
    tcsetattr(0, TCSANOW, &_orig_term);
    printf("\033[?25h");
    fflush(stdout);
    _raw = 0;
}

void raw_mode(void) {
    if (_raw) return;
    struct termios t;
    tcgetattr(0, &_orig_term);
    atexit(restore_term);
    t = _orig_term;
    t.c_lflag &= ~(ECHO | ICANON);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    printf("\033[?25l");
    fflush(stdout);
    _raw = 1;
}

void normal_mode(void) {
    if (!_raw) return;
    tcsetattr(0, TCSANOW, &_orig_term);
    printf("\033[?25h");
    fflush(stdout);
    _raw = 0;
}

int kbhit(void)       { char c; return read(0, &c, 1) > 0; }
void flush_stdin(void) { char c; while (read(0, &c, 1) > 0); }
void gotoxy(int col, int row) { printf("\033[%d;%dH", row+1, col+1); }
void clrscr(void) { printf("\033[2J\033[H"); fflush(stdout); }

/* ================================================================
   Grid types and operations
   ================================================================ */
typedef int Grid[FIELD][FIELD];
#define GSIZ (sizeof(int) * FIELD * FIELD)

void grid_copy(Grid dst, Grid src) { memcpy(dst, src, GSIZ); }
void grid_zero(Grid g)             { memset(g,   0,   GSIZ); }

int nbrs(Grid g, int x, int y) {
    int n = 0, dx, dy;
    for (dx = -1; dx <= 1; dx++)
        for (dy = -1; dy <= 1; dy++) {
            if (!dx && !dy) continue;
            n += g[(x+dx+FIELD)%FIELD][(y+dy+FIELD)%FIELD];
        }
    return n;
}

void grid_step(Grid cur, Grid nxt) {
    int i, j, n;
    for (i = 0; i < FIELD; i++)
        for (j = 0; j < FIELD; j++) {
            n = nbrs(cur, i, j);
            nxt[i][j] = cur[i][j] ? (n==2||n==3) : (n==3);
        }
}

int grid_alive(Grid g) {
    int n = 0, i, j;
    for (i = 0; i < FIELD; i++)
        for (j = 0; j < FIELD; j++) n += g[i][j];
    return n;
}

uint64_t grid_hash(Grid g) {
    uint64_t h = 14695981039346656037ULL;
    unsigned char *p = (unsigned char *)g;
    size_t i;
    for (i = 0; i < GSIZ; i++) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

/* Заполняет центральную часть FIELD×FIELD случайными значениями
   в квадрате size×size */
void grid_rand_center(Grid g, int size) {
    int s = (FIELD - size) / 2, i, j;
    grid_zero(g);
    for (i = s; i < s+size; i++)
        for (j = s; j < s+size; j++)
            g[i][j] = rand() % 2;
}

/* ================================================================
   Типы исходов
   ================================================================ */
typedef enum { EXTINCT=0, STABLE, PERIODIC, LONG_RUN } Outcome;

const char *out_name(Outcome o) {
    switch (o) {
        case EXTINCT:  return "Вымирание";
        case STABLE:   return "Стабильная конфигурация";
        case PERIODIC: return "Периодическая конфигурация";
        default:       return "Долгоживущая (100+ поколений)";
    }
}
const char *out_key(Outcome o) {
    switch (o) {
        case EXTINCT:  return "EXTINCT";
        case STABLE:   return "STABLE";
        case PERIODIC: return "PERIODIC";
        default:       return "LONG_RUN";
    }
}
Outcome out_parse(const char *s) {
    if (!strcmp(s, "EXTINCT"))  return EXTINCT;
    if (!strcmp(s, "STABLE"))   return STABLE;
    if (!strcmp(s, "PERIODIC")) return PERIODIC;
    return LONG_RUN;
}

/* ================================================================
   Структура сохранённой конфигурации
   ================================================================ */
typedef struct {
    Grid    grid;
    int     rand_size;
    Outcome type;
    int     generations;
    int     period;
} Config;

/* ================================================================
   Файловые операции
   ================================================================ */
void config_write(FILE *f, Config *c) {
    int i, j;
    fprintf(f, "[CONFIG]\n");
    fprintf(f, "type=%s\n",        out_key(c->type));
    fprintf(f, "generations=%d\n", c->generations);
    fprintf(f, "period=%d\n",      c->period);
    fprintf(f, "rand_size=%d\n",   c->rand_size);
    fprintf(f, "grid:\n");
    for (i = 0; i < FIELD; i++) {
        for (j = 0; j < FIELD; j++) fputc('0' + c->grid[i][j], f);
        fputc('\n', f);
    }
    fprintf(f, "[/CONFIG]\n\n");
}

int configs_read(const char *path, Config *arr, int max_n) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int n = 0, in_cfg = 0, rd_grid = 0, grow = 0;
    Config cur;
    while (fgets(line, sizeof(line), f) && n < max_n) {
        int len = strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len] = 0;
        if (!strcmp(line, "[CONFIG]")) {
            memset(&cur, 0, sizeof(cur)); in_cfg = 1; rd_grid = 0; grow = 0;
        } else if (!strcmp(line, "[/CONFIG]")) {
            if (in_cfg) arr[n++] = cur;
            in_cfg = 0;
        } else if (in_cfg) {
            if (!strcmp(line, "grid:")) {
                rd_grid = 1; grow = 0;
            } else if (rd_grid && grow < FIELD) {
                int j;
                for (j = 0; j < FIELD && line[j]; j++)
                    cur.grid[grow][j] = line[j] - '0';
                grow++;
            } else {
                char k[64], v[128];
                if (sscanf(line, "%63[^=]=%127s", k, v) == 2) {
                    if      (!strcmp(k,"type"))        cur.type        = out_parse(v);
                    else if (!strcmp(k,"generations")) cur.generations = atoi(v);
                    else if (!strcmp(k,"period"))      cur.period      = atoi(v);
                    else if (!strcmp(k,"rand_size"))   cur.rand_size   = atoi(v);
                }
            }
        }
    }
    fclose(f);
    return n;
}

/* ================================================================
   Тихая симуляция (для автопоиска)
   ================================================================ */
typedef struct { Outcome type; int gen; int period; } SimRes;

SimRes sim_silent(Grid start) {
    Grid cur, nxt;
    SimRes r = {LONG_RUN, 100, 0};
    /* Храним хэши последних MAX_GENS+1 состояний */
    static uint64_t hist[MAX_GENS+1];
    int g;

    grid_copy(cur, start);
    hist[0] = grid_hash(cur);

    for (g = 1; g <= MAX_GENS; g++) {
        grid_step(cur, nxt);
        grid_copy(cur, nxt);

        if (!grid_alive(cur)) {
            r.type = EXTINCT; r.gen = g; return r;
        }

        uint64_t h = grid_hash(cur);
        /* Ищем период — сравниваем с последними 120 состояниями */
        int from = g > 120 ? g-120 : 0;
        int i;
        for (i = from; i < g; i++) {
            if (hist[i] == h) {
                r.period = g - i;
                r.gen    = g;
                r.type   = (r.period == 1) ? STABLE : PERIODIC;
                return r;
            }
        }
        hist[g] = h;

        /* 100 поколений без вымирания/стабилизации → долгоживущая */
        if (g == 100) { r.type = LONG_RUN; r.gen = 100; return r; }
    }
    r.type = LONG_RUN; r.gen = MAX_GENS;
    return r;
}

/* ================================================================
   Отрисовка
   ================================================================ */
void draw_field(Grid g) {
    int i, j;
    gotoxy(0, 0);
    printf("+");
    for (j = 0; j < FIELD; j++) printf("--");
    printf("+\033[K\n");
    for (i = 0; i < FIELD; i++) {
        printf("|");
        for (j = 0; j < FIELD; j++)
            printf(g[i][j] ? "\033[32m##\033[0m" : "  ");
        printf("|\033[K\n");
    }
    printf("+");
    for (j = 0; j < FIELD; j++) printf("--");
    printf("+\033[K\n");
}

void draw_status(int gen, int alive, int classified, Outcome type, int period) {
    gotoxy(0, FIELD+2);
    printf(" Поколение: \033[1m%-5d\033[0m  Живых: \033[1m%-5d\033[0m  ", gen, alive);
    if (classified) {
        printf("\033[1;33m%s\033[0m", out_name(type));
        if (type == PERIODIC) printf("  (период: %d)", period);
    } else {
        printf("симуляция идёт...");
    }
    printf("\033[K\n");
    printf(" Любая клавиша — остановить\033[K");
    fflush(stdout);
}

/* ================================================================
   Интерактивная симуляция с визуализацией
   ================================================================ */
void run_with_grid(Grid init, int rand_size) {
    Grid cur, nxt, snap;
    static uint64_t hist[MAX_GENS+2];
    int gen = 0, period = 0, classified = 0, terminal = 0;
    Outcome type = LONG_RUN;

    grid_copy(cur,  init);
    grid_copy(snap, init);
    hist[0] = grid_hash(cur);

    clrscr();
    raw_mode();

    for (;;) {
        draw_field(cur);
        draw_status(gen, grid_alive(cur), classified, type, period);

        if (terminal) { usleep(3000000); break; }
        usleep(120000);
        if (kbhit()) break;

        grid_step(cur, nxt);
        grid_copy(cur, nxt);
        gen++;

        /* Классификация */
        if (!classified) {
            if (!grid_alive(cur)) {
                type = EXTINCT; classified = 1; terminal = 1;
            } else if (gen <= MAX_GENS) {
                uint64_t h = grid_hash(cur);
                int from = gen > 120 ? gen-120 : 0;
                int i;
                for (i = from; i < gen && !classified; i++) {
                    if (hist[i] == h) {
                        period = gen - i;
                        type   = (period == 1) ? STABLE : PERIODIC;
                        classified = 1; terminal = 1;
                    }
                }
                if (gen <= MAX_GENS) hist[gen] = h;
                /* Если живёт 100+ поколений — «долгоживущая», но продолжаем показ */
                if (!classified && gen >= 100) {
                    type = LONG_RUN; classified = 1;
                }
            }
        }
        if (gen >= MAX_GENS) terminal = 1;
    }

    flush_stdin();
    normal_mode();
    printf("\033[%d;1H\033[J", FIELD+3);  /* перейти к строке статуса и очистить */

    printf(" Результат: \033[1;33m%s\033[0m", out_name(type));
    if (type == PERIODIC) printf("  (период: %d)", period);
    printf(", поколений: %d\n", gen);

    printf(" Сохранить начальную конфигурацию в файл %s? (y/n): ", SAVEFILE);
    fflush(stdout);
    int c = getchar(); while (getchar() != '\n' && !feof(stdin));

    if (c == 'y' || c == 'Y') {
        Config sc;
        grid_copy(sc.grid, snap);
        sc.rand_size   = rand_size;
        sc.type        = type;
        sc.generations = gen;
        sc.period      = period;
        FILE *f = fopen(SAVEFILE, "a");
        if (f) { config_write(f, &sc); fclose(f); printf(" Сохранено!\n"); }
        else     printf(" Ошибка: не удалось открыть файл!\n");
    }
}

/* ================================================================
   Автоматический поиск всех четырёх типов конфигураций
   ================================================================ */
void auto_search(void) {
    int found[4] = {0,0,0,0};
    Config best[4];
    int total = 0, all = 0;

    clrscr();
    raw_mode();

    while (!all && !kbhit()) {
        int sz = 3 + rand() % 8;   /* случайный размер от 3 до 10 */
        Grid g, snap;
        grid_rand_center(g, sz);
        grid_copy(snap, g);

        SimRes r   = sim_silent(g);
        int was_new = !found[(int)r.type];
        total++;

        if (was_new) {
            found[(int)r.type] = 1;
            grid_copy(best[(int)r.type].grid, snap);
            best[(int)r.type].rand_size   = sz;
            best[(int)r.type].type        = r.type;
            best[(int)r.type].generations = r.gen;
            best[(int)r.type].period      = r.period;
        }

        all = found[0] && found[1] && found[2] && found[3];

        if (was_new || all || total % 200 == 0) {
            const Outcome ord[] = {EXTINCT, STABLE, PERIODIC, LONG_RUN};
            int i;
            gotoxy(0, 0);
            printf(" === Автопоиск интересных конфигураций ===\033[K\n\n");
            printf(" Проверено конфигураций: \033[1m%d\033[0m\033[K\n\n", total);
            for (i = 0; i < 4; i++) {
                Outcome o2 = ord[i];
                printf("  [%s] %s  ",
                       found[(int)o2] ? "\033[32m+\033[0m" : " ",
                       out_name(o2));
                if (found[(int)o2]) {
                    printf("поколений: %-5d", best[(int)o2].generations);
                    if (o2 == PERIODIC) printf("  период: %d", best[(int)o2].period);
                }
                printf("\033[K\n");
            }
            if (all)
                printf("\033[K\n \033[1;32mВсе 4 типа найдены! Сохранение...\033[0m\033[K\n");
            else
                printf("\033[K\n [Любая клавиша — остановить]\033[K\n");
            fflush(stdout);
        }
    }

    if (all) usleep(1500000);
    flush_stdin();
    normal_mode();

    /* Сохраняем все найденные конфиги */
    int saved = 0, i;
    for (i = 0; i < 4; i++) {
        if (found[i]) {
            FILE *f = fopen(SAVEFILE, "a");
            if (f) { config_write(f, &best[i]); fclose(f); saved++; }
        }
    }
    printf("\n Найдено %d из 4 типов. Сохранено %d конфигураций в %s\n",
           found[0]+found[1]+found[2]+found[3], saved, SAVEFILE);
    printf(" Нажмите Enter для продолжения...\n");
    while (getchar() != '\n');
}

/* ================================================================
   Загрузка и запуск сохранённой конфигурации
   ================================================================ */
void load_and_run(void) {
    Config arr[64];
    int n = configs_read(SAVEFILE, arr, 64);
    if (n == 0) {
        printf("\n Файл %s не найден или пустой.\n", SAVEFILE);
        printf(" Нажмите Enter...\n");
        while (getchar() != '\n');
        return;
    }

    int i;
    printf("\n === Сохранённые конфигурации ===\n\n");
    for (i = 0; i < n; i++) {
        printf("  %2d. [%-10s]  поколений: %-5d",
               i+1, out_key(arr[i].type), arr[i].generations);
        if (arr[i].type == PERIODIC)
            printf("  период: %d", arr[i].period);
        printf("\n");
    }

    printf("\n Номер для запуска (1-%d, 0 — отмена): ", n);
    fflush(stdout);
    int ch = 0;
    scanf("%d", &ch);
    while (getchar() != '\n');
    if (ch < 1 || ch > n) return;

    Config *c = &arr[ch-1];
    printf("\n Запуск: %s...\n", out_name(c->type));
    sleep(1);
    run_with_grid(c->grid, c->rand_size);
}

/* ================================================================
   Главное меню
   ================================================================ */
int main(void) {
    srand((unsigned)time(NULL));
    atexit(restore_term);

    for (;;) {
        printf("\033[2J\033[H");
        printf("\n\033[1;36m+==========================+\033[0m\n");
        printf("\033[1;36m|      Игра «Жизнь»        |\033[0m\n");
        printf("\033[1;36m+==========================+\033[0m\n\n");
        printf("  1. Случайная симуляция\n");
        printf("  2. Автопоиск интересных конфигураций\n");
        printf("  3. Загрузить конфигурацию из файла\n");
        printf("  0. Выход\n\n");
        printf("  Выбор: ");
        fflush(stdout);

        int c = getchar();
        while (getchar() != '\n' && !feof(stdin));

        if (c == '1') {
            printf("\n  Размер случайной области (3-10, Enter = случайный): ");
            fflush(stdout);
            char buf[32];
            fgets(buf, sizeof(buf), stdin);
            int sz = atoi(buf);
            if (sz < 3 || sz > 10) sz = 3 + rand() % 8;
            Grid g;
            grid_rand_center(g, sz);
            run_with_grid(g, sz);
        } else if (c == '2') {
            auto_search();
        } else if (c == '3') {
            load_and_run();
        } else if (c == '0') {
            break;
        }
    }

    printf("\n До свидания!\n");
    return 0;
}

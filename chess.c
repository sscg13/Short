/* chess.c - main entry, FEN parsing, standalone perft/search test harness */

#include "engine.h"

static int pchar(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
    }
    return EMPTY;
}

void parse_fen(Pos *p, const char *s) {
    int i, rank = 7, file = 0;

    for (i = 0; i < 128; i++) p->board[i] = EMPTY;
    p->ks[0] = p->ks[1] = -1;

    while (*s && *s != ' ') {
        char c = *s++;
        if (c == '/') { rank--; file = 0; }
        else if (c >= '1' && c <= '8') file += c - '0';
        else { p->board[rank * 16 + file] = pchar(c); file++; }
    }
    s++;
    p->side = (*s == 'b') ? 1 : 0;
    s += 2;

    p->castle = 0;
    while (*s && *s != ' ') {
        char c = *s++;
        if (c == 'K') p->castle |= 1;
        else if (c == 'Q') p->castle |= 2;
        else if (c == 'k') p->castle |= 4;
        else if (c == 'q') p->castle |= 8;
    }
    s++;

    p->ep = -1;
    while (*s && *s != ' ') {
        char c = *s++;
        if (c >= 'a' && c <= 'h') {
            int f = c - 'a';
            if (*s >= '1' && *s <= '8')
                p->ep = ((*s) - '1') * 16 + f;
        }
    }

    g_half = 0; g_full = 1;
    if (*s) {
        int h = 0, f = 1;
        if (sscanf(s, "%d %d", &h, &f) >= 1) { g_half = h; g_full = f; }
    }

    for (i = 0; i < 128; i++) {
        if (p->board[i] == WK) p->ks[0] = i;
        else if (p->board[i] == BK) p->ks[1] = i;
    }
}

static const char *fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
};

static const unsigned long expv[6][6] = {
    { 20, 400, 8902, 197281, 4865609, 119060324 },
    { 48, 2039, 97862, 4085603, 193690690, 0 },
    { 14, 191, 2812, 43238, 674624, 11030083 },
    { 6, 264, 9467, 422333, 15833292, 706045033 },
    { 44, 1486, 62379, 2103487, 89941194, 0 },
    { 46, 2079, 89890, 3894594, 164075551, 0 }
};

int main(int argc, char **argv) {
    int maxd = 5, test = 0, i, splitsel = 0;
    Pos pos;
    long nodes;
    clock_t t0, t1;
    double secs;

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "xboard") == 0))
        return xboard_main();

    if (argc > 1 && argv[1][0] == 's') {
        int s_depth = 5;
        test = 0;
        if (argc > 2) s_depth = atoi(argv[2]);
        if (argc > 3) test = atoi(argv[3]);
        if (s_depth < 1) s_depth = 1;
        if (s_depth > 10) s_depth = 10;
        if (test == 8) {
            FILE *f = fopen("fen.txt", "r");
            char buf[128];
            if (f) {
                fgets(buf, 128, f);
                fclose(f);
                parse_fen(&pos, buf);
            } else {
                parse_fen(&pos, fens[0]);
            }
        } else if (test >= 0 && test <= 5) {
            parse_fen(&pos, fens[test]);
        } else {
            parse_fen(&pos, fens[0]);
        }
        search_root(&pos, s_depth);
        return 0;
    }

    if (argc > 1) maxd = atoi(argv[1]);
    if (argc > 2) test = atoi(argv[2]);
    if (argc > 3 && argv[3][0] == 's') splitsel = 1;
    if (maxd < 1) maxd = 1;
    if (maxd > 6) maxd = 6;

    if (test == 9 && argc > 4)
        parse_fen(&pos, argv[4]);
    else if (test == 8) {
        FILE *f = fopen("fen.txt", "r");
        char buf[128];
        if (f) {
            fgets(buf, 128, f);
            fclose(f);
            parse_fen(&pos, buf);
        } else {
            parse_fen(&pos, fens[0]);
        }
    } else if (test >= 0 && test <= 5)
        parse_fen(&pos, fens[test]);
    else
        parse_fen(&pos, fens[0]);

    printf("pos=%d CLOCKS_PER_SEC=%d\n", test + 1, (int)CLOCKS_PER_SEC);

    if (splitsel) {
        unsigned int *list = movebuf[7];
        int n = gen_moves(&pos, list);
        for (i = 0; i < n; i++) {
            Undo u;
            int us;
            long c;
            do_make(&pos, list[i], &u);
            us = pos.side ^ 1;
            if (!is_attacked(&pos, pos.ks[us], pos.side))
                c = perft(&pos, maxd - 1);
            else
                c = -1;
            undo_move(&pos, list[i], &u);
            printf("m=%02X%02X p=%d -> %lu\n",
                   mfrom(list[i]), mto(list[i]), mpro(list[i], us ? 8 : 0), c);
        }
        return 0;
    }

    for (i = 1; i <= maxd; i++) {
        unsigned long want, got;
        t0 = clock();
        nodes = perft(&pos, i);
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        got = (unsigned long)nodes;
        want = expv[test][i - 1];
        printf("perft(%d)=%lu  %8.2fs  %6lu nps  %s\n",
               i, got, secs,
               secs > 0 ? (unsigned long)((double)nodes / secs) : 0UL,
               (want && got != want) ? "*** WRONG ***" : "ok");
    }
    return 0;
}

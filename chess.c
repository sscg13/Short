#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { EMPTY = 0, WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
       BP = 9, BN = 10, BB = 11, BR = 12, BQ = 13, BK = 14 };

#define CO(p) ((p) & 8)      /* 0 = white, 8 = black */
#define TY(p) ((p) & 7)

/* move encoding (32-bit long)
   bits 0..6   to square
   bits 7..13  from square
   bits 14..15 flags (F_EP, F_CASTLE)
   bits 16..23 promotion piece (0 = none) */
#define F_EP     0x4000
#define F_CASTLE 0x8000
#define mto(m)   ((int)((m) & 0x7F))
#define mfrom(m) ((int)((m) >> 7) & 0x7F)
#define mprom(m) ((int)((m) >> 16) & 0xFF)
#define MK(t, f, fl, pr) ((long)((t) & 0x7F) | ((long)((f) & 0x7F) << 7) | \
                          ((long)(fl)) | ((long)(pr) << 16))

typedef struct {
    int board[128];   /* 0x88 board */
    int side;         /* 0 white, 1 black */
    int castle;       /* bit0 WK bit1 WQ bit2 BK bit3 BQ */
    int ep;           /* en-passant target square, or -1 */
    int ks[2];        /* king squares */
} Pos;

typedef struct { int cap, castle, ep; } Undo;

static const int kn[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int ki[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const int rb[4] = { -16, 1, 16, -1 };
static const int bb[4] = { -17, -15, 15, 17 };
static const int qd[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const int pw[4] = { WN, WB, WR, WQ };
static const int pb[4] = { BN, BB, BR, BQ };
static const int mval[8] = { 0, 100, 320, 330, 500, 900, 0 };

static long movebuf[12][256];

#define INF  30000
#define MATE 29000

static long nodes_search;

/* ------------------------------------------------------------------ */
/* make / unmake                                                      */
/* ------------------------------------------------------------------ */

static void do_make(Pos *p, long m, Undo *u) {
    int from = mfrom(m), to = mto(m);
    int piece = p->board[from];
    int promo = mprom(m);

    u->cap = p->board[to];
    u->castle = p->castle;
    u->ep = p->ep;

    p->board[to] = promo ? promo : piece;
    p->board[from] = EMPTY;

    if (m & F_CASTLE) {
        if (to == 0x06)      { p->board[0x05] = WR; p->board[0x07] = EMPTY; }
        else if (to == 0x02) { p->board[0x03] = WR; p->board[0x00] = EMPTY; }
        else if (to == 0x76) { p->board[0x75] = BR; p->board[0x77] = EMPTY; }
        else if (to == 0x72) { p->board[0x73] = BR; p->board[0x70] = EMPTY; }
    } else if (m & F_EP) {
        if (p->side == 0) p->board[to - 16] = EMPTY;
        else              p->board[to + 16] = EMPTY;
    }

    if (TY(piece) == 6) {
        if (from == 0x04) p->castle &= ~3;
        else if (from == 0x74) p->castle &= ~12;
    }
    if (from == 0x00 || to == 0x00) p->castle &= ~2;
    if (from == 0x07 || to == 0x07) p->castle &= ~1;
    if (from == 0x70 || to == 0x70) p->castle &= ~8;
    if (from == 0x77 || to == 0x77) p->castle &= ~4;

    p->ep = -1;
    if (piece == WP && to == from + 32) p->ep = from + 16;
    else if (piece == BP && to == from - 32) p->ep = from - 16;

    if (TY(piece) == 6) p->ks[p->side] = to;

    p->side ^= 1;
}

static void undo_move(Pos *p, long m, Undo *u) {
    int from = mfrom(m), to = mto(m);
    int piece = p->board[to];

    p->side ^= 1;

    if (mprom(m)) piece = (p->side == 0) ? WP : BP;
    p->board[from] = piece;
    p->board[to] = u->cap;

    if (m & F_CASTLE) {
        if (to == 0x06)      { p->board[0x07] = WR; p->board[0x05] = EMPTY; }
        else if (to == 0x02) { p->board[0x00] = WR; p->board[0x03] = EMPTY; }
        else if (to == 0x76) { p->board[0x77] = BR; p->board[0x75] = EMPTY; }
        else if (to == 0x72) { p->board[0x70] = BR; p->board[0x73] = EMPTY; }
    } else if (m & F_EP) {
        if (p->side == 0) p->board[to - 16] = BP;
        else              p->board[to + 16] = WP;
    }

    p->castle = u->castle;
    p->ep = u->ep;
    if (TY(piece) == 6) p->ks[p->side] = from;
}

/* ------------------------------------------------------------------ */
/* attacks                                                            */
/* ------------------------------------------------------------------ */

static int is_attacked(Pos *p, int sq, int by) {
    int i, to, d;
    int pc;

    if (by == 0) {
        to = sq - 17; if ((to & 0x88) == 0 && p->board[to] == WP) return 1;
        to = sq - 15; if ((to & 0x88) == 0 && p->board[to] == WP) return 1;
    } else {
        to = sq + 15; if ((to & 0x88) == 0 && p->board[to] == BP) return 1;
        to = sq + 17; if ((to & 0x88) == 0 && p->board[to] == BP) return 1;
    }

    for (i = 0; i < 8; i++) {
        to = sq + kn[i];
        if ((to & 0x88) == 0 && p->board[to] == (by ? BN : WN)) return 1;
    }
    for (i = 0; i < 8; i++) {
        to = sq + ki[i];
        if ((to & 0x88) == 0 && p->board[to] == (by ? BK : WK)) return 1;
    }
    for (i = 0; i < 4; i++) {
        d = rb[i]; to = sq + d;
        while ((to & 0x88) == 0) {
            pc = p->board[to];
            if (pc) {
                if (by ? (pc == BR || pc == BQ) : (pc == WR || pc == WQ)) return 1;
                break;
            }
            to += d;
        }
    }
    for (i = 0; i < 4; i++) {
        d = bb[i]; to = sq + d;
        while ((to & 0x88) == 0) {
            pc = p->board[to];
            if (pc) {
                if (by ? (pc == BB || pc == BQ) : (pc == WB || pc == WQ)) return 1;
                break;
            }
            to += d;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* move generation (pseudo-legal)                                     */
/* ------------------------------------------------------------------ */

static int gen_moves(Pos *p, long *list) {
    int n = 0, from, to, to2, pc, pt, us = p->side, them = us ^ 1;
    int i, d, fwd, r;

    for (from = 0; from < 128; from++) {
        pc = p->board[from];
        if (!pc) continue;
        if (CO(pc) != (us ? 8 : 0)) continue;
        pt = TY(pc);

        if (pt == WP || pt == BP) {
            int isW = (pc == WP), prank, srank;
            fwd = isW ? 16 : -16;
            r = from >> 4;
            prank = isW ? 6 : 1;   /* row index of from-square for a promoting push */
            srank = isW ? 1 : 6;   /* row index of from-square for a double push */

            to = from + fwd;
            if ((to & 0x88) == 0 && p->board[to] == EMPTY) {
                if (r == prank) {
                    for (i = 0; i < 4; i++)
                        list[n++] = MK(to, from, 0, isW ? pw[i] : pb[i]);
                } else {
                    list[n++] = MK(to, from, 0, 0);
                    if (r == srank) {
                        to2 = from + 2 * fwd;
                        if ((to2 & 0x88) == 0 && p->board[to2] == EMPTY)
                            list[n++] = MK(to2, from, 0, 0);
                    }
                }
            }

            for (d = -1; d <= 1; d += 2) {
                to = from + fwd + d;
                if ((to & 0x88) != 0) continue;
                if (to == p->ep) {
                    list[n++] = MK(to, from, F_EP, 0);
                } else {
                    int tgt = p->board[to];
                    if (tgt && CO(tgt) == (them ? 8 : 0)) {
                        if (r == prank) {
                            for (i = 0; i < 4; i++)
                                list[n++] = MK(to, from, 0, isW ? pw[i] : pb[i]);
                        } else {
                            list[n++] = MK(to, from, 0, 0);
                        }
                    }
                }
            }
        } else if (pt == WN || pt == BN) {
            for (i = 0; i < 8; i++) {
                to = from + kn[i];
                if ((to & 0x88) != 0) continue;
                if (!p->board[to] || CO(p->board[to]) != CO(pc))
                    list[n++] = MK(to, from, 0, 0);
            }
        } else if (pt == WK || pt == BK) {
            for (i = 0; i < 8; i++) {
                to = from + ki[i];
                if ((to & 0x88) != 0) continue;
                if (!p->board[to] || CO(p->board[to]) != CO(pc))
                    list[n++] = MK(to, from, 0, 0);
            }
        } else {
            const int *dirs;
            int ndir;
            if (pt == WB || pt == BB)      { dirs = bb; ndir = 4; }
            else if (pt == WR || pt == BR) { dirs = rb; ndir = 4; }
            else                           { dirs = qd; ndir = 8; }
            for (i = 0; i < ndir; i++) {
                d = dirs[i];
                to = from + d;
                while ((to & 0x88) == 0) {
                    int tgt = p->board[to];
                    if (!tgt) {
                        list[n++] = MK(to, from, 0, 0);
                    } else {
                        if (CO(tgt) != CO(pc)) list[n++] = MK(to, from, 0, 0);
                        break;
                    }
                    to += d;
                }
            }
        }
    }

    if (us == 0) {
        if ((p->castle & 1) && p->board[0x04] == WK && p->board[0x07] == WR &&
            p->board[0x05] == EMPTY && p->board[0x06] == EMPTY &&
            !is_attacked(p, 0x04, 1) && !is_attacked(p, 0x05, 1) && !is_attacked(p, 0x06, 1))
            list[n++] = MK(0x06, 0x04, F_CASTLE, 0);
        if ((p->castle & 2) && p->board[0x04] == WK && p->board[0x00] == WR &&
            p->board[0x01] == EMPTY && p->board[0x02] == EMPTY && p->board[0x03] == EMPTY &&
            !is_attacked(p, 0x04, 1) && !is_attacked(p, 0x03, 1) && !is_attacked(p, 0x02, 1))
            list[n++] = MK(0x02, 0x04, F_CASTLE, 0);
    } else {
        if ((p->castle & 4) && p->board[0x74] == BK && p->board[0x77] == BR &&
            p->board[0x75] == EMPTY && p->board[0x76] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x75, 0) && !is_attacked(p, 0x76, 0))
            list[n++] = MK(0x76, 0x74, F_CASTLE, 0);
        if ((p->castle & 8) && p->board[0x74] == BK && p->board[0x70] == BR &&
            p->board[0x71] == EMPTY && p->board[0x72] == EMPTY && p->board[0x73] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x73, 0) && !is_attacked(p, 0x72, 0))
            list[n++] = MK(0x72, 0x74, F_CASTLE, 0);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* perft                                                              */
/* ------------------------------------------------------------------ */

static long perft(Pos *p, int depth) {
    long nodes = 0;
    long *list = movebuf[depth];
    int n = gen_moves(p, list);
    int i;

    for (i = 0; i < n; i++) {
        Undo u;
        int us;
        do_make(p, list[i], &u);
        us = p->side ^ 1;              /* mover */
        if (!is_attacked(p, p->ks[us], p->side)) {   /* enemy of mover */
            if (depth == 1) nodes++;
            else nodes += perft(p, depth - 1);
        }
        undo_move(p, list[i], &u);
    }
    return nodes;
}

/* ------------------------------------------------------------------ */
/* evaluation (material only)                                         */
/* ------------------------------------------------------------------ */

static int evaluate(Pos *p) {
    int score = 0, sq;
    for (sq = 0; sq < 128; sq++) {
        int pc = p->board[sq];
        if (!pc) continue;
        if (CO(pc) == 0) score += mval[TY(pc)];
        else             score -= mval[TY(pc)];
    }
    return (p->side == 0) ? score : -score;   /* negamax: side to move */
}

/* ------------------------------------------------------------------ */
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static int alphabeta(Pos *p, int depth, int alpha, int beta) {
    long *list = movebuf[depth];
    int n = gen_moves(p, list);
    int best = -INF, legal = 0, i;

    nodes_search++;

    for (i = 0; i < n; i++) {
        Undo u;
        int us, score;
        do_make(p, list[i], &u);
        us = p->side ^ 1;                        /* mover */
        if (!is_attacked(p, p->ks[us], p->side)) {
            legal = 1;
            if (depth <= 0) score = -evaluate(p);
            else score = -alphabeta(p, depth - 1, -beta, -alpha);
            if (score > best) best = score;
            if (best > alpha) alpha = best;
            undo_move(p, list[i], &u);
            if (alpha >= beta) break;            /* beta cutoff */
        } else {
            undo_move(p, list[i], &u);
        }
    }

    if (!legal)
        return is_attacked(p, p->ks[p->side], p->side ^ 1) ? -(MATE - depth) : 0;
    return best;
}

static void search_root(Pos *p, int maxdepth) {
    int d;
    for (d = 1; d <= maxdepth; d++) {
        long *list = movebuf[11];                /* dedicated root row */
        int n = gen_moves(p, list);
        int alpha = -INF, beta = INF, i;
        int bestscore = -INF, bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        nodes_search = 0;
        t0 = clock();
        for (i = 0; i < n; i++) {
            Undo u;
            int us, score;
            do_make(p, list[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                score = -alphabeta(p, d - 1, -beta, -alpha);
                if (score > bestscore) { bestscore = score; bf = mfrom(list[i]); bt = mto(list[i]); }
                if (score > alpha) alpha = score;
                undo_move(p, list[i], &u);
                if (alpha >= beta) break;
            } else {
                undo_move(p, list[i], &u);
            }
        }
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        printf("depth %2d  score %5d  move %02X%02X  %8ld nodes  %7.2fs\n",
               d, bestscore, bf, bt, nodes_search, secs);
    }
}

/* ------------------------------------------------------------------ */
/* FEN                                                                */
/* ------------------------------------------------------------------ */

static int pchar(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
    }
    return EMPTY;
}

static void parse_fen(Pos *p, const char *s) {
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

    for (i = 0; i < 128; i++) {
        if (p->board[i] == WK) p->ks[0] = i;
        else if (p->board[i] == BK) p->ks[1] = i;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

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

    if (argc > 1 && argv[1][0] == 's') {
        int s_depth = 5;
        test = 0;
        if (argc > 2) s_depth = atoi(argv[2]);
        if (argc > 3) test = atoi(argv[3]);
        if (s_depth < 1) s_depth = 1;
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
        long *list = movebuf[7];
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
                   mfrom(list[i]), mto(list[i]), mprom(list[i]), c);
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

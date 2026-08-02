/* search.c - board representation, move generation, alpha-beta, perft */

#include "engine.h"

static const int kn[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int ki[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const int rb[4] = { -16, 1, 16, -1 };
static const int bb[4] = { -17, -15, 15, 17 };
static const int qd[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const int pw[4] = { WN, WB, WR, WQ };
static const int pb[4] = { BN, BB, BR, BQ };
static const int mval[8] = { 0, 100, 320, 330, 500, 900, 0 };

unsigned int movebuf[12][256];

volatile int stop_now = 0;
long deadline = 0;                   /* ms deadline, 0 = no limit */

static long nodes_search;
static unsigned long rep_path[64];      /* position sigs along the current search line */
static int rep_n;

/* ------------------------------------------------------------------ */
/* make / unmake                                                      */
/* ------------------------------------------------------------------ */

void do_make(Pos *p, unsigned int m, Undo *u) {
    int from = mfrom(m), to = mto(m);
    int fl = mfl(m);
    int piece = p->board[from];
    int promo = ispromo(m) ? (CO(piece) | (fl + 1)) : 0;

    u->cap = p->board[to];
    u->castle = p->castle;
    u->ep = p->ep;

    p->board[to] = promo ? promo : piece;
    p->board[from] = EMPTY;

    if (fl == MF_CASTLE) {
        if (to == 0x06)      { p->board[0x05] = WR; p->board[0x07] = EMPTY; }
        else if (to == 0x02) { p->board[0x03] = WR; p->board[0x00] = EMPTY; }
        else if (to == 0x76) { p->board[0x75] = BR; p->board[0x77] = EMPTY; }
        else if (to == 0x72) { p->board[0x73] = BR; p->board[0x70] = EMPTY; }
    } else if (fl == MF_EP) {
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

void undo_move(Pos *p, unsigned int m, Undo *u) {
    int from = mfrom(m), to = mto(m);
    int fl = mfl(m);
    int piece = p->board[to];

    p->side ^= 1;

    if (ispromo(m)) piece = (p->side == 0) ? WP : BP;
    p->board[from] = piece;
    p->board[to] = u->cap;

    if (fl == MF_CASTLE) {
        if (to == 0x06)      { p->board[0x07] = WR; p->board[0x05] = EMPTY; }
        else if (to == 0x02) { p->board[0x00] = WR; p->board[0x03] = EMPTY; }
        else if (to == 0x76) { p->board[0x77] = BR; p->board[0x75] = EMPTY; }
        else if (to == 0x72) { p->board[0x70] = BR; p->board[0x73] = EMPTY; }
    } else if (fl == MF_EP) {
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

int is_attacked(Pos *p, int sq, int by) {
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

int gen_moves(Pos *p, unsigned int *list) {
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
                    list[n++] = MK(to, from, MF_EP, 0);
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
            list[n++] = MK(0x06, 0x04, MF_CASTLE, 0);
        if ((p->castle & 2) && p->board[0x04] == WK && p->board[0x00] == WR &&
            p->board[0x01] == EMPTY && p->board[0x02] == EMPTY && p->board[0x03] == EMPTY &&
            !is_attacked(p, 0x04, 1) && !is_attacked(p, 0x03, 1) && !is_attacked(p, 0x02, 1))
            list[n++] = MK(0x02, 0x04, MF_CASTLE, 0);
    } else {
        if ((p->castle & 4) && p->board[0x74] == BK && p->board[0x77] == BR &&
            p->board[0x75] == EMPTY && p->board[0x76] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x75, 0) && !is_attacked(p, 0x76, 0))
            list[n++] = MK(0x76, 0x74, MF_CASTLE, 0);
        if ((p->castle & 8) && p->board[0x74] == BK && p->board[0x70] == BR &&
            p->board[0x71] == EMPTY && p->board[0x72] == EMPTY && p->board[0x73] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x73, 0) && !is_attacked(p, 0x72, 0))
            list[n++] = MK(0x72, 0x74, MF_CASTLE, 0);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* perft                                                              */
/* ------------------------------------------------------------------ */

long perft(Pos *p, int depth) {
    long nodes = 0;
    unsigned int *list = movebuf[depth];
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
/* position signature (for repetition)                                */
/* ------------------------------------------------------------------ */

unsigned long pos_sig(Pos *p) {
    unsigned long h = 0x811C9DC5UL;
    int i;
    for (i = 0; i < 128; i++)
        if (p->board[i])
            h = (h * 16777619UL) ^ (unsigned long)((i << 4) | p->board[i]);
    h = (h * 16777619UL) ^ (unsigned long)p->side;
    h = (h * 16777619UL) ^ (unsigned long)(p->castle & 0xF);
    h = (h * 16777619UL) ^ (unsigned long)(p->ep + 1);
    return h;
}

/* ------------------------------------------------------------------ */
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static int alphabeta(Pos *p, int depth, int alpha, int beta, int ply, int half) {
    unsigned int *list = movebuf[depth];
    int n = gen_moves(p, list);
    int best = -INF, legal = 0, i;

    nodes_search++;
    if ((nodes_search & 0x3FF) == 0 && deadline > 0 && (long)clock() >= deadline)
        stop_now = 1;
    if (stop_now) return best;

    /* threefold repetition (game history + current line) is a draw */
    {
        unsigned long sig = pos_sig(p);
        int prior = 0;
        for (i = 0; i < g_sigs_n; i++)
            if (g_sigs[i] == sig) { prior++; if (prior >= 2) break; }
        if (prior < 2)
            for (i = 0; i < rep_n; i++)
                if (rep_path[i] == sig) { prior++; if (prior >= 2) break; }
        if (prior >= 2) return 0;               /* 3rd occurrence */
        rep_path[rep_n++] = sig;
    }

    /* 50-move rule: 100 half-moves without a pawn move or capture is a draw */
    if (half >= 100) return 0;

    for (i = 0; i < n; i++) {
        Undo u;
        int us, score, pc, is_cap, child_half;
        pc = p->board[mfrom(list[i])];           /* moving piece, before the make */
        do_make(p, list[i], &u);
        us = p->side ^ 1;                        /* mover */
        if (!is_attacked(p, p->ks[us], p->side)) {
            legal = 1;
            is_cap = (u.cap != EMPTY) || (mfl(list[i]) == MF_EP);
            child_half = (TY(pc) == 1 || is_cap) ? 0 : half + 1;
            if (depth <= 0) score = -evaluate(p);
            else score = -alphabeta(p, depth - 1, -beta, -alpha, ply + 1, child_half);
            if (score > best) best = score;
            if (best > alpha) alpha = best;
            undo_move(p, list[i], &u);
            if (alpha >= beta) break;            /* beta cutoff */
        } else {
            undo_move(p, list[i], &u);
        }
    }

    rep_n--;                                     /* pop this node */
    if (!legal)
        /* mate scores: -(MATE - ply) so the root prefers the SHORTEST mate */
        return is_attacked(p, p->ks[p->side], p->side ^ 1) ? -(MATE - ply) : 0;
    return best;
}

void search_root(Pos *p, int maxdepth) {
    int d;
    for (d = 1; d <= maxdepth; d++) {
        unsigned int *list = movebuf[11];                /* dedicated root row */
        int n = gen_moves(p, list);
        int alpha = -INF, beta = INF, i;
        int bestscore = -INF, bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        nodes_search = 0;
        t0 = clock();
        rep_n = 0;
        for (i = 0; i < n; i++) {
            Undo u;
            int us, score;
            do_make(p, list[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                int pc = p->board[mfrom(list[i])];   /* moving piece, before the make */
                int is_cap = (u.cap != EMPTY) || (mfl(list[i]) == MF_EP);
                int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
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

/* iterative deepening root search; returns best move (0 if aborted before any depth) */
unsigned int think(Pos *p, int maxdepth) {
    int d;
    unsigned int bestm = 0;
    long t0 = (long)clock();
    dbgf("think begin maxdepth=%d deadline=%ld\n", maxdepth, deadline);
    for (d = 1; d <= maxdepth; d++) {
        unsigned int *list = movebuf[11];
        int n = gen_moves(p, list);
        int alpha = -INF, beta = INF, i, bsc = -INF;
        unsigned int bm = 0;
        if (deadline > 0 && (long)clock() >= deadline) break;
        nodes_search = 0;
        stop_now = 0;
        rep_n = 0;
        for (i = 0; i < n; i++) {
            Undo u; int us, score;
            do_make(p, list[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                int pc = p->board[mfrom(list[i])];   /* moving piece, before the make */
                int is_cap = (u.cap != EMPTY) || (mfl(list[i]) == MF_EP);
                int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                if (score > bsc) { bsc = score; bm = list[i]; }
                if (score > alpha) alpha = score;
                undo_move(p, list[i], &u);
                if (alpha >= beta) break;
            } else undo_move(p, list[i], &u);
            if (stop_now) break;
        }
        if (stop_now) break;
        bestm = bm;
        if (post_on) {
            printf("%d %d %ld %ld\n", d, bsc, (long)clock() - t0, nodes_search);
            fflush(stdout);
        }
    }
    dbgf("think end bestm=%04X stop=%d d=%d\n", (unsigned)bestm, (int)stop_now, d - 1);
    return bestm;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#if defined(__WATCOMC__)
#include <process.h>
#define DBG_PID() ((long)getpid())
#elif defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
#define DBG_PID() ((long)_getpid())
#else
#include <unistd.h>
#define DBG_PID() ((long)getpid())
#endif

enum { EMPTY = 0, WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
       BP = 9, BN = 10, BB = 11, BR = 12, BQ = 13, BK = 14 };

#define CO(p) ((p) & 8)      /* 0 = white, 8 = black */
#define TY(p) ((p) & 7)

/* move encoding (16-bit unsigned int)
   bits 0..5   from square (compact rank*8+file, 0..63)
   bits 6..11  to square (compact)
   bits 12..15 flags (MF_*, distinct values compared via mfl())

   flags: 0 = quiet/capture, 1..4 = promote to N/B/R/Q
   (piece type = flag + 1), 5 = en passant, 6 = castling */
#define MF_PROMO_N 1
#define MF_PROMO_B 2
#define MF_PROMO_R 3
#define MF_PROMO_Q 4
#define MF_EP      5
#define MF_CASTLE  6

#define sq2c(s)  (((s) & 7) | (((s) >> 1) & 56))    /* 0x88 sq -> compact 0..63 */
#define c2sq(c)  (((c) & 7) | (((c) & 56) << 1))    /* compact -> 0x88 sq */
#define mfl(m)   (((unsigned)(m) >> 12) & 0xF)
#define mfrom(m) c2sq((unsigned)(m) & 0x3F)
#define mto(m)   c2sq((((unsigned)(m) >> 6) & 0x3F))
#define ispromo(m) (mfl(m) >= MF_PROMO_N && mfl(m) <= MF_PROMO_Q)
#define mpro(m, col) (ispromo(m) ? ((col) | (mfl(m) + 1)) : 0)  /* promo piece, col 0/8 */
#define MK(t, f, fl, pr) ((unsigned int)((sq2c(f) & 0x3F) | \
                          ((sq2c(t) & 0x3F) << 6) | \
                          ((((pr) ? (int)(TY(pr) - 1) : (int)(fl)) & 0xF) << 12)))

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

static unsigned int movebuf[12][256];

#define INF  30000
#define MATE 29000

static long nodes_search;

/* ---- CECP / xboard game state ---- */
static Pos gpos;
static int force_mode = 1, game_over = 0, post_on = 0;
static int g_half, g_full;              /* halfmove clock, fullmove number */
static unsigned long g_sigs[1024];      /* position signatures for repetition */
static int g_sigs_n;
static int xb_st = 0, xb_time_cs = 0;
static int xb_level_mps = 0, xb_level_inc = 0;   /* "level mps base inc" control */
static struct { unsigned int m; Undo u; int half, full, sn; } gstack[1024];
static int gstack_n;
static volatile int stop_now = 0;
static long deadline = 0;               /* ms deadline, 0 = no limit */

static unsigned long rep_path[64];      /* position sigs along the current search line */
static int rep_n;

#define TIME_MARGIN_MS 30               /* stop searching this many ms early so the
                                           move is reported before the clock runs out.
                                           Must exceed Windows' clock() granularity
                                           (~15.6 ms) + output overhead or we lose on
                                           time at tight clocks. */

static FILE *fdbg;                      /* protocol debug log (chess_debug.txt) */

static void dbgf(const char *fmt, ...) {
    va_list ap;
    if (!fdbg) return;
    va_start(ap, fmt);
    vfprintf(fdbg, fmt, ap);
    va_end(ap);
    fflush(fdbg);
}

static void xb_outf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (fdbg) {
        va_start(ap, fmt);
        vfprintf(fdbg, fmt, ap);
        va_end(ap);
        fprintf(fdbg, "\n");
        fflush(fdbg);
    }
}

/* ------------------------------------------------------------------ */
/* make / unmake                                                      */
/* ------------------------------------------------------------------ */

static void do_make(Pos *p, unsigned int m, Undo *u) {
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

static void undo_move(Pos *p, unsigned int m, Undo *u) {
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

static int gen_moves(Pos *p, unsigned int *list) {
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

static long perft(Pos *p, int depth) {
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
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static unsigned long pos_sig(Pos *p);

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

static void search_root(Pos *p, int maxdepth) {
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

/* ------------------------------------------------------------------ */
/* CECP / xboard protocol                                              */
/* ------------------------------------------------------------------ */

static const char *start_fen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static unsigned long pos_sig(Pos *p) {
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

static int legal_move(Pos *p, unsigned int m) {
    Undo u;
    int us, ok;
    do_make(p, m, &u);
    us = p->side ^ 1;
    ok = !is_attacked(p, p->ks[us], p->side);
    undo_move(p, m, &u);
    return ok;
}

static void move_to_coord(unsigned int m, char *buf) {
    static const char pn[] = " PNBRQK";
    int f = mfrom(m), t = mto(m);
    buf[0] = 'a' + (f & 7);
    buf[1] = '1' + (f >> 4);
    buf[2] = 'a' + (t & 7);
    buf[3] = '1' + (t >> 4);
    if (ispromo(m)) { buf[4] = pn[mfl(m) + 1]; buf[5] = 0; }
    else buf[4] = 0;
}

static unsigned int parse_coord(Pos *p, const char *s) {
    unsigned int *list = movebuf[8];
    int n = gen_moves(p, list);
    int i, pr = 0;
    if (s[0] < 'a' || s[0] > 'h' || s[2] < 'a' || s[2] > 'h') return 0;
    {
        int f = (s[1] - '1') * 16 + (s[0] - 'a');
        int t = (s[3] - '1') * 16 + (s[2] - 'a');
        switch (s[4]) {
            case 'n': case 'N': pr = WN; break;
            case 'b': case 'B': pr = WB; break;
            case 'r': case 'R': pr = WR; break;
            case 'q': case 'Q': pr = WQ; break;
        }
        for (i = 0; i < n; i++) {
            if ((int)mfrom(list[i]) != f) continue;
            if ((int)mto(list[i]) != t) continue;
            if (!legal_move(p, list[i])) continue;
            if (pr) { if (ispromo(list[i]) && mfl(list[i]) + 1 == TY(pr)) return list[i]; }
            else if (!ispromo(list[i])) return list[i];
        }
    }
    return 0;
}

static unsigned int parse_castle(Pos *p, const char *s) {
    unsigned int *list = movebuf[8];
    int n = gen_moves(p, list), i, want = 0;
    if ((s[0] == 'O' || s[0] == '0') && s[1] == '-' && (s[2] == 'O' || s[2] == '0')) {
        if (s[3] == '-' && (s[4] == 'O' || s[4] == '0')) want = 0x02;
        else want = 0x06;
    } else return 0;
    for (i = 0; i < n; i++)
        if (mfl(list[i]) == MF_CASTLE && (int)(mto(list[i]) & 0x0F) == want)
            return list[i];
    return 0;
}

static int draw_claim(Pos *p) {
    int i, occ = 0, minors = 0;
    unsigned long s;
    if (g_half >= 100) { dbgf("draw_claim: halfmove g_half=%d\n", g_half); return 1; }
    s = pos_sig(p);
    {
        int c = 0;
        for (i = 0; i < g_sigs_n; i++)
            if (g_sigs[i] == s) {
                if (++c >= 3) {
                    dbgf("draw_claim: repetition sig=%08lX count=%d g_sigs_n=%d\n",
                         s, c, g_sigs_n);
                    return 1;
                }
            }
    }
    for (i = 0; i < 128; i++) {
        int pc = p->board[i];
        int ty;
        if (!pc) continue;
        ty = TY(pc);
        if (ty == 1) return 0;
        if (ty == 4 || ty == 5) return 0;
        if (ty == 2 || ty == 3) minors++;
        occ++;
    }
    if (occ <= 2 || minors <= 1) {
        dbgf("draw_claim: material occ=%d minors=%d\n", occ, minors);
        return 1;
    }
    return 0;
}

static void apply_move(Pos *p, unsigned int m) {
    Undo u;
    int pc = p->board[mfrom(m)];
    int half = g_half, full = g_full, sn = g_sigs_n;
    do_make(p, m, &u);
    if (gstack_n < 1024) {
        gstack[gstack_n].m = m; gstack[gstack_n].u = u;
        gstack[gstack_n].half = half; gstack[gstack_n].full = full;
        gstack[gstack_n].sn = sn; gstack_n++;
    }
    if (TY(pc) == 1 || u.cap) g_half = 0; else g_half++;
    if (CO(pc) == 8) g_full++;
    if (g_sigs_n < 1024) g_sigs[g_sigs_n++] = pos_sig(p);
}

static void unapply(Pos *p) {
    if (gstack_n <= 0) return;
    gstack_n--;
    undo_move(p, gstack[gstack_n].m, &gstack[gstack_n].u);
    g_half = gstack[gstack_n].half;
    g_full = gstack[gstack_n].full;
    g_sigs_n = gstack[gstack_n].sn;
}

/* iterative deepening root search; returns best move (0 if aborted before any depth) */
static unsigned int think(Pos *p, int maxdepth) {
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

static void xb_go(void) {
    unsigned int *list = movebuf[11];
    int n = gen_moves(&gpos, list);
    int i, legal = 0;
    unsigned int m = 0, first = 0;
    char b[8];
    if (game_over) return;
    force_mode = 0;
    for (i = 0; i < n; i++) {
        if (!legal_move(&gpos, list[i])) continue;
        legal = 1;
        if (!first) first = list[i];
    }
    if (!legal) {
        if (is_attacked(&gpos, gpos.ks[gpos.side], gpos.side ^ 1))
            xb_outf("%s {checkmate}", gpos.side == 0 ? "0-1" : "1-0");
        else
            xb_outf("1/2-1/2 {stalemate}");
        game_over = 1;
        return;
    }
    if (draw_claim(&gpos)) {
        xb_outf("1/2-1/2 {draw}");
        game_over = 1;
        return;
    }
    {
        long budget = 3000;
        long remaining_ms = (long)xb_time_cs * 10;
        if (xb_st > 0) {
            budget = (long)xb_st * 1000;         /* fixed seconds per move */
        } else if (xb_level_mps > 0) {
            /* tournament control "level mps base inc": remaining/mps + increment */
            if (remaining_ms > 0)
                budget = remaining_ms / xb_level_mps + (long)xb_level_inc * 1000;
        } else if (xb_time_cs > 0) {
            long cs = xb_time_cs / 40;           /* ~1/40 of remaining clock */
            if (cs < 50) cs = 50;
            budget = cs * 10;                    /* centiseconds -> ms */
        }
        /* never allocate more than the time actually left (minus the margin) */
        if (remaining_ms > TIME_MARGIN_MS + 100 && budget > remaining_ms - TIME_MARGIN_MS)
            budget = remaining_ms - TIME_MARGIN_MS;
        if (budget < 100) budget = 100;
        deadline = (long)clock() + budget - TIME_MARGIN_MS;
        dbgf("go: side=%d st=%d time_cs=%ld level=%d+%d budget=%ld deadline=%ld\n",
             gpos.side, xb_st, (long)xb_time_cs, xb_level_mps, xb_level_inc,
             budget, deadline);
    }
    m = think(&gpos, 10);
    deadline = 0;
    stop_now = 0;
    if (m == 0) m = first;
    move_to_coord(m, b);
    xb_outf("move %s", b);
    apply_move(&gpos, m);
}

static void xb_reset(void) {
    parse_fen(&gpos, start_fen);
    g_sigs_n = 0; gstack_n = 0;
    g_sigs[g_sigs_n++] = pos_sig(&gpos);
    force_mode = 1; game_over = 0;
    stop_now = 0; deadline = 0;          /* keep xb_time_cs/xb_st/post_on: WinBoard
                                            re-sends them at each new game anyway */
}

static char *skipsp(char *s) { while (*s == ' ') s++; return s; }

static int xboard_main(void) {
    char line[256];
    xb_reset();
    while (fgets(line, sizeof(line), stdin)) {
        char *p = line;
        int i;
        for (i = 0; line[i] && line[i] != '\n' && line[i] != '\r'; i++);
        line[i] = 0;
        while (*p == ' ') p++;
        dbgf(">> %s\n", p);
        if (strncmp(p, "xboard", 6) == 0) {
        } else if (strncmp(p, "protover", 8) == 0) {
            xb_outf("feature myname=\"Chess86\" setboard=1 usermove=1 ping=1 playother=1 done=1");
        } else if (strncmp(p, "new", 3) == 0) {
            xb_reset();
        } else if (strncmp(p, "setboard", 8) == 0) {
            parse_fen(&gpos, skipsp(p + 8));
            g_sigs_n = 0; gstack_n = 0;
            g_sigs[g_sigs_n++] = pos_sig(&gpos);
        } else if (strncmp(p, "force", 5) == 0) {
            force_mode = 1;
        } else if (strncmp(p, "playother", 9) == 0) {
            xb_go();
        } else if (strncmp(p, "go", 2) == 0) {
            xb_go();
        } else if (strncmp(p, "usermove", 8) == 0 || strncmp(p, "move ", 5) == 0) {
            char *mv = skipsp(p + (strncmp(p, "usermove", 8) == 0 ? 8 : 5));
            unsigned int m = parse_coord(&gpos, mv);
            if (!m) m = parse_castle(&gpos, mv);
            if (!m) { xb_outf("Illegal move: %s", mv); }
            else {
                dbgf("usermove '%s' ok m=%04X\n", mv, (unsigned)m);
                apply_move(&gpos, m);
                /* CECP play mode: `go`/`playother` persist until `force`/`new`/`result`;
                   after the opponent's move WinBoard does not send `go` again, so we must
                   search on the usermove itself. */
                if (!force_mode && !game_over) xb_go();
            }
        } else if (strncmp(p, "time", 4) == 0) {
            xb_time_cs = atoi(skipsp(p + 4));
        } else if (strncmp(p, "otim", 4) == 0) {
        } else if (strncmp(p, "st", 2) == 0) {
            xb_st = atoi(skipsp(p + 2));
        } else if (strncmp(p, "level", 5) == 0) {
            int mps = 0, base = 0, inc = 0;
            sscanf(skipsp(p + 5), "%d %d %d", &mps, &base, &inc);
            xb_level_mps = mps;
            xb_level_inc = inc;
        } else if (strncmp(p, "ping", 4) == 0) {
            xb_outf("pong %s", skipsp(p + 4));
        } else if (strncmp(p, "quit", 4) == 0) {
            dbgf("quit received\n");
            return 0;
        } else if (strncmp(p, "result", 6) == 0) {
            game_over = 1;
        } else if (strncmp(p, "draw", 4) == 0) {
            xb_outf("1/2-1/2 {Engine accepts draw offer}");
        } else if (strncmp(p, "post", 4) == 0) {
            post_on = 1;
        } else if (strncmp(p, "nopost", 6) == 0) {
            post_on = 0;
        } else if (strncmp(p, "remove", 6) == 0 || strncmp(p, "undo", 4) == 0) {
            unapply(&gpos);
        } else if (p[0] == '?') {
            stop_now = 1;
        } else {
            /* hard/easy/random/name/accepted/rejected/variant/analyze/exit/bk/edit/hint: ignored */
        }
    }
    dbgf("stdin closed, exiting\n");
    return 0;
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

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "xboard") == 0)) {
        int r;
        char fname[64];
        /* per-process log file so two self-play engines don't clobber each other */
        sprintf(fname, "chess_debug_%ld.txt", DBG_PID());
        fdbg = fopen(fname, "w");
        if (fdbg) { fprintf(fdbg, "== chess32 protocol debug ==\n"); fflush(fdbg); }
        r = xboard_main();
        if (fdbg) fclose(fdbg);
        return r;
    }

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

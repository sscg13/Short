/* search.c - alpha-beta search and iterative deepening */

#include "engine.h"

volatile int stop_now = 0;
long deadline = 0;                   /* ms deadline, 0 = no limit */

static long nodes_search;
static unsigned long rep_path[64];      /* position sigs along the current search line */
static int rep_n;

static int killers[MAXPLY][2];          /* two killer moves per ply (quiet only) */

#define MAX_QDEPTH 8    /* longest capture chain past the search leaf; guards against
                           qsearch explosions (deep recapture lines) */

/* root move list + scores, reused across iterative-deepening iterations */
static unsigned int root_m[256];
static int root_score[256];
static int root_n;

/* selection sort the root moves by last iteration's score (best first) */
static void sort_root(void) {
    int i, j, best;
    for (i = 0; i < root_n - 1; i++) {
        best = i;
        for (j = i + 1; j < root_n; j++)
            if (root_score[j] > root_score[best]) best = j;
        if (best != i) {
            unsigned int tm = root_m[i]; int ts = root_score[i];
            root_m[i] = root_m[best]; root_m[best] = tm;
            root_score[i] = root_score[best]; root_score[best] = ts;
        }
    }
}

/* ------------------------------------------------------------------ */
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static int qsearch(Pos *p, int alpha, int beta, int ply, int half, int qd);

static int alphabeta(Pos *p, int depth, int alpha, int beta, int ply, int half) {
    MGen mg;
    unsigned int m;
    int best = -INF, legal = 0;

    nodes_search++;
    if ((nodes_search & 0x3FF) == 0 && deadline > 0 && (long)clock() >= deadline)
        stop_now = 1;
    if (stop_now) return best;

    /* threefold repetition (game history + current line) is a draw */
    {
        unsigned long sig = pos_sig(p);
        int prior = 0;
        int i;
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

    mgen_init(p, &mg, ply, killers[ply][0], killers[ply][1], 0);   /* ttm empty for now */

    while ((m = next_move(p, &mg)) != 0) {
        Undo u;
        int us, score, pc, is_cap, child_half;
        pc = p->board[mfrom(m)];                 /* moving piece, before the make */
        do_make(p, m, &u);
        us = p->side ^ 1;                        /* mover */
        if (!is_attacked(p, p->ks[us], p->side)) {
            legal = 1;
            is_cap = (u.cap != EMPTY) || (mfl(m) == MF_EP);
            child_half = (TY(pc) == 1 || is_cap) ? 0 : half + 1;
            if (depth <= 0) score = -qsearch(p, -beta, -alpha, ply + 1, child_half, MAX_QDEPTH);
            else score = -alphabeta(p, depth - 1, -beta, -alpha, ply + 1, child_half);
            if (score > best) best = score;
            if (best > alpha) alpha = best;
            undo_move(p, m, &u);
            if (alpha >= beta) {
                /* killer: a true quiet (no promo/ep/castle flag, empty target) */
                if (mfl(m) == 0 && u.cap == EMPTY) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
                break;                            /* beta cutoff */
            }
        } else {
            undo_move(p, m, &u);
        }
    }

    rep_n--;                                     /* pop this node */
    if (!legal)
        /* mate scores: -(MATE - ply) so the root prefers the SHORTEST mate */
        return is_attacked(p, p->ks[p->side], p->side ^ 1) ? -(MATE - ply) : 0;
    return best;
}

/* ------------------------------------------------------------------ */
/* quiescence search                                                  */
/* ------------------------------------------------------------------ */

/* Stand-pat alpha-beta over captures only, ordered by MVV-LVA (reuses the staged
   generator in caps-only mode). Called at every alpha-beta horizon (depth 0) so the
   eval is stable and material wins/losses don't hide behind the horizon.
   If the side to move is in check, stand-pat is invalid: generate ALL legal moves
   (full staged generator) to find evasions, and score a mate properly.
   Returns the score from the side-to-move's point of view (negamax). */
static int qsearch(Pos *p, int alpha, int beta, int ply, int half, int qd) {
    MGen mg;
    unsigned int m;
    int in_check, stand, legal = 0;

    nodes_search++;
    if ((nodes_search & 0x3FF) == 0 && deadline > 0 && (long)clock() >= deadline)
        stop_now = 1;
    if (stop_now) return evaluate(p);
    if (qd <= 0) return evaluate(p);             /* ply budget spent: static eval */
    if (ply >= MAXPLY - 4) return evaluate(p);   /* stay clear of movebuf aux rows */
    if (half >= 100) return 0;

    in_check = is_attacked(p, p->ks[p->side], p->side ^ 1);
    if (!in_check) {
        stand = evaluate(p);
        if (stand >= beta) return stand;         /* stand-pat cutoff */
        if (stand > alpha) alpha = stand;
    }

    if (in_check) mgen_init(p, &mg, ply, 0, 0, 0);  /* all legal evasions */
    else          mgen_init_q(p, &mg, ply);          /* captures only, MVV-LVA */

    while ((m = next_move(p, &mg)) != 0) {
        Undo u;
        int us, score, pc, is_cap, child_half;
        pc = p->board[mfrom(m)];
        do_make(p, m, &u);
        us = p->side ^ 1;
        if (!is_attacked(p, p->ks[us], p->side)) {
            legal = 1;
            is_cap = (u.cap != EMPTY) || (mfl(m) == MF_EP);
            child_half = (TY(pc) == 1 || is_cap) ? 0 : half + 1;
            score = -qsearch(p, -beta, -alpha, ply + 1, child_half, qd - 1);
            undo_move(p, m, &u);
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) break;         /* beta cutoff */
            }
        } else {
            undo_move(p, m, &u);
        }
    }

    if (!legal && in_check)
        return -(MATE - ply);                    /* mated in the qsearch */
    return alpha;
}

void search_root(Pos *p, int maxdepth) {
    int d, i;
    root_n = gen_moves(p, root_m);
    for (i = 0; i < root_n; i++) root_score[i] = 0;
    if (nnue_enabled) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        int alpha = -INF, beta = INF;
        int bestscore = -INF, bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        nodes_search = 0;
        t0 = clock();
        rep_n = 0;
        sort_root();
        for (i = 0; i < root_n; i++) {
            Undo u;
            int us, score;
            do_make(p, root_m[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                int pc = p->board[mfrom(root_m[i])];   /* moving piece, before the make */
                int is_cap = (u.cap != EMPTY) || (mfl(root_m[i]) == MF_EP);
                int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                root_score[i] = score;
                if (score > bestscore) { bestscore = score; bf = mfrom(root_m[i]); bt = mto(root_m[i]); }
                if (score > alpha) alpha = score;
                undo_move(p, root_m[i], &u);
                if (alpha >= beta) break;
            } else {
                undo_move(p, root_m[i], &u);
            }
        }
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        printf("depth %2d  score %5d  move %02X%02X  %8ld nodes  %7.2fs\n",
               d, bestscore, bf, bt, nodes_search, secs);
    }
    nnue_active = 0;
}

/* iterative deepening root search; returns best move (0 if aborted before any depth) */
unsigned int think(Pos *p, int maxdepth) {
    int d, i;
    unsigned int bestm = 0;
    long t0 = (long)clock();
    dbgf("think begin maxdepth=%d deadline=%ld\n", maxdepth, deadline);
    root_n = gen_moves(p, root_m);
    for (i = 0; i < root_n; i++) root_score[i] = 0;
    if (nnue_enabled) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        int alpha = -INF, beta = INF, bsc = -INF;
        unsigned int bm = 0;
        if (deadline > 0 && (long)clock() >= deadline) break;
        nodes_search = 0;
        stop_now = 0;
        rep_n = 0;
        sort_root();
        for (i = 0; i < root_n; i++) {
            Undo u; int us, score;
            do_make(p, root_m[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                int pc = p->board[mfrom(root_m[i])];   /* moving piece, before the make */
                int is_cap = (u.cap != EMPTY) || (mfl(root_m[i]) == MF_EP);
                int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                root_score[i] = score;
                if (score > bsc) { bsc = score; bm = root_m[i]; }
                if (score > alpha) alpha = score;
                undo_move(p, root_m[i], &u);
                if (alpha >= beta) break;
            } else undo_move(p, root_m[i], &u);
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
    nnue_active = 0;
    return bestm;
}

/* ------------------------------------------------------------------ */
/* OpenBench bench                                                     */
/* ------------------------------------------------------------------ */
/* OpenBench invokes the engine as "<binary> bench", then scans stdout
   (from the bottom) for a "nodes" count and an "nps" value. The bench
   MUST be deterministic: fixed positions searched to a fixed depth, no
   time-based cutoffs (deadline stays 0). Run "chess bench [depth]"
   locally to tune BENCH_DEPTH so the whole run lands in ~1-5 seconds. */

#if defined(__WATCOMC__) && !defined(__386__)
#define BENCH_DEPTH 4   /* 16-bit DOS build: a manual DOSBox bench stays ~40s
                           (depth 5 = 8M nodes ~= 4 min there; native builds are ~110x faster) */
#else
#define BENCH_DEPTH 5   /* calibrated: 8 positions at depth 5 ~= 2s on this machine
                           (depth 6 ~= 8s; qsearch makes search nodes cheap per-node) */
#endif

static const char *bench_fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 0 1",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 w - - 0 1",
    "4rrk1/ppp1bppp/2np1n2/8/2BP4/2N2N2/PP2BPPP/R4RK1 w - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "8/2p5/3p1k2/4p3/4P3/3P4/5PKP/8 b - - 0 1"
};

#define BENCH_N ((int)(sizeof(bench_fens) / sizeof(bench_fens[0])))

int bench(int depth) {
    int i, d, k;
    long total_nodes = 0;
    clock_t b0, b1;
    double bsecs;

    if (depth < 1) depth = BENCH_DEPTH;
    if (depth > 20) depth = 20;
    deadline = 0;                                /* keep the search timing-independent */
    stop_now = 0;

    b0 = clock();
    for (i = 0; i < BENCH_N; i++) {
        Pos p;
        long pos_nodes = 0;
        int bestscore = -INF, bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        parse_fen(&p, bench_fens[i]);
        g_sigs_n = 0;                            /* no game-history repetitions */
        memset(killers, 0, sizeof killers);
        root_n = gen_moves(&p, root_m);
        for (k = 0; k < root_n; k++) root_score[k] = 0;
        if (nnue_enabled) { nnue_reset(&p); nnue_active = 1; }

        t0 = clock();
        for (d = 1; d <= depth; d++) {
            int alpha = -INF, beta = INF, bsc = -INF;
            unsigned int bm = 0;
            nodes_search = 0;
            rep_n = 0;
            stop_now = 0;
            sort_root();
            for (k = 0; k < root_n; k++) {
                Undo u; int us, score;
                do_make(&p, root_m[k], &u);
                us = p.side ^ 1;
                if (!is_attacked(&p, p.ks[us], p.side)) {
                    int pc = p.board[mfrom(root_m[k])];
                    int is_cap = (u.cap != EMPTY) || (mfl(root_m[k]) == MF_EP);
                    int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                    score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                    root_score[k] = score;
                    if (score > bsc) { bsc = score; bm = root_m[k]; }
                    if (score > alpha) alpha = score;
                    undo_move(&p, root_m[k], &u);
                    if (alpha >= beta) break;
                } else undo_move(&p, root_m[k], &u);
            }
            pos_nodes += nodes_search;
            if (d == depth) { bestscore = bsc; bf = mfrom(bm); bt = mto(bm); }
        }
        nnue_active = 0;
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        total_nodes += pos_nodes;
        printf("position %2d/%d  depth %2d  score %5d  move %02X%02X  n=%10ld  t=%7.2fs\n",
               i + 1, BENCH_N, depth, bestscore, bf, bt, pos_nodes, secs);
    }
    b1 = clock();
    bsecs = (double)(b1 - b0) / (double)CLOCKS_PER_SEC;

    /* these two lines must stay the last output: OpenBench matches them
       scanning up from the bottom of stdout */
    printf("\nNodes searched : %ld\n", total_nodes);
    printf("NPS: %ld\n", (long)(total_nodes / (bsecs > 0.0 ? bsecs : 1.0)));
    return 0;
}

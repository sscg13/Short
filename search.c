/* search.c - alpha-beta search and iterative deepening */

#include "engine.h"

volatile int stop_now = 0;
long deadline = 0;                   /* ms deadline, 0 = no limit */

#if defined(PROFILE) || defined(VCLOCK)
long c_anodes = 0;                   /* alphabeta entries */
long c_qnodes = 0;                   /* qsearch entries */
#endif

static long nodes_search;
static unsigned long rep_path[MAX_REP_PATH];   /* position sigs along the current search line */
static int rep_n;

/* principal-variation lines for the `post` search-info output (CECP §10).
   pv[ply][0..pv_len[ply]-1] is the best line from ply, in move encoding. */
static unsigned int pv[MAXPLY + 1][MAXPLY];
static int pv_len[MAXPLY + 1];

static int killers[MAXPLY][2];          /* two killer moves per ply (quiet only) */

#define MAX_QDEPTH 8    /* longest capture chain past the search leaf; guards against
                           qsearch explosions (deep recapture lines) */

/* root move list + scores, reused across iterative-deepening iterations */
static unsigned int root_m[256];
static Score root_score[256];
static int root_n;

/* selection sort the root moves by last iteration's score (best first) */
static void sort_root(void) {
    int i, j, best;
    for (i = 0; i < root_n - 1; i++) {
        best = i;
        for (j = i + 1; j < root_n; j++)
            if (root_score[j] > root_score[best]) best = j;
        if (best != i) {
            unsigned int tm = root_m[i]; Score ts = root_score[i];
            root_m[i] = root_m[best]; root_m[best] = tm;
            root_score[i] = root_score[best]; root_score[best] = ts;
        }
    }
}

/* ------------------------------------------------------------------ */
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static Score qsearch(Pos *p, Score alpha, Score beta, int ply, int half, int qd);

static Score alphabeta(Pos *p, int depth, Score alpha, Score beta, int ply, int half) {
    MGen mg;
    unsigned int m;
    Score best = -INF;
    int legal = 0, in_check = 0;

    PCOUNT(c_anodes);
    nodes_search++;
    vtotal_nodes++;
    if ((nodes_search & 0x3FF) == 0 && deadline > 0 && (long)clock() >= deadline)
        stop_now = 1;
    if (vtime_mode && vclock_budget_hit())
        stop_now = 1;
    if (stop_now) return best;
    if (ply >= MAXPLY) return evaluate(p);   /* hard depth cap: never index past pv/killers/movebuf */
    pv_len[ply] = 0;                                 /* no best line yet at this ply */

    /* threefold repetition (game history + current line) is a draw */
    {
        unsigned long sig = pos_sig(p);
        int prior = 0;
        int i;
        for (i = 0; i < g_sigs_n && i < MAX_G_SIGS; i++)
            if (g_sigs[i] == sig) { prior++; if (prior >= 2) break; }
        if (prior < 2)
            for (i = 0; i < rep_n && i < MAX_REP_PATH; i++)
                if (rep_path[i] == sig) { prior++; if (prior >= 2) break; }
        if (prior >= 2) return 0;               /* 3rd occurrence */
        if (rep_n < MAX_REP_PATH) rep_path[rep_n++] = sig;
    }

    /* 50-move rule: 100 half-moves without a pawn move or capture is a draw */
    if (half >= MAX_HALF) { rep_n--; return 0; }

    /* in-check status of the side to move: a non-king, non-EP move from a
       square NOT on the king's rank/file/diagonal is then always legal (it can
       neither leave a check unresolved nor open a new line), so its legality
       is_attacked test can be skipped. */
    in_check = is_attacked(p, p->ks[p->side], p->side ^ 1);

    mgen_init(p, &mg, ply, killers[ply][0], killers[ply][1], 0);   /* ttm empty for now */

    while ((m = next_move(p, &mg)) != 0) {
        Undo u;
        int us, pc, is_cap, child_half, legal_move = 0;
        Score score;
        pc = p->board[mfrom(m)];                 /* moving piece, before the make */
        do_make(p, m, &u);
        us = p->side ^ 1;                        /* mover */
        /* Legality: if not in check, a non-king, non-EP move from a square off
           the mover king's lines cannot leave the king attacked, so skip the
           is_attacked test. Otherwise (in check, king move, EP, or an aligned
           from-square) the real test decides. */
        if (!in_check && TY(pc) != 6 && mfl(m) != MF_EP &&
            !sq_on_king_line(p, mfrom(m), us))
            legal_move = 1;
        else if (!is_attacked(p, p->ks[us], p->side))
            legal_move = 1;
        if (legal_move) {
            legal = 1;
            is_cap = (u.cap != EMPTY) || (mfl(m) == MF_EP);
            child_half = (TY(pc) == 1 || is_cap) ? 0 : half + 1;
            if (depth <= 0) score = -qsearch(p, -beta, -alpha, ply + 1, child_half, MAX_QDEPTH);
            else score = -alphabeta(p, depth - 1, -beta, -alpha, ply + 1, child_half);
            if (score > best) {
                best = score;
                if (ply < MAXPLY) {                  /* extend this node's PV with the child's */
                    int pl = pv_len[ply + 1], k;
                    pv[ply][0] = m;
                    for (k = 0; k < pl && k < MAXPLY - 1; k++) pv[ply][k + 1] = pv[ply + 1][k];
                    pv_len[ply] = (pl >= MAXPLY) ? MAXPLY : pl + 1;
                }
            }
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

    if (rep_n > 0) rep_n--;                  /* pop this node */
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
static Score qsearch(Pos *p, Score alpha, Score beta, int ply, int half, int qd) {
    MGen mg;
    unsigned int m;
    int in_check, legal = 0;
    Score stand;

    PCOUNT(c_qnodes);
    nodes_search++;
    vtotal_nodes++;
    if ((nodes_search & 0x3FF) == 0 && deadline > 0 && (long)clock() >= deadline)
        stop_now = 1;
    if (vtime_mode && vclock_budget_hit())
        stop_now = 1;
    if (stop_now) return evaluate(p);
    if (qd <= 0) return evaluate(p);             /* ply budget spent: static eval */
    if (ply >= MAXPLY - 4) return evaluate(p);   /* stay clear of movebuf aux rows */
    if (half >= MAX_HALF) return 0;
    pv_len[ply] = 0;                             /* qsearch is a leaf: no continuation */

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
        int us, pc, is_cap, child_half, legal_move = 0;
        Score score;
        pc = p->board[mfrom(m)];
        do_make(p, m, &u);
        us = p->side ^ 1;
        if (!in_check && TY(pc) != 6 && mfl(m) != MF_EP &&
            !sq_on_king_line(p, mfrom(m), us))
            legal_move = 1;
        else if (!is_attacked(p, p->ks[us], p->side))
            legal_move = 1;
        if (legal_move) {
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
    if (nnue_ensure_default()) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        Score alpha = -INF, beta = INF;
        Score bestscore = -INF;
        int bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        nodes_search = 0;
        t0 = clock();
        rep_n = 0;
        sort_root();
        for (i = 0; i < root_n; i++) {
            Undo u;
            int us;
            Score score;
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

/* CECP §10 thinking-output score: normal centipawns, but mate scores are
   reported as 100000+N ("mate in N moves") / -100000-N ("mated in N moves").
   The engine's internal mate score is MATE-ply (ply = distance from root to
   the terminal position), so N = (ply+1)/2. */
static long score_to_cecp(Score score) {
    if (score >= MATE - MAXPLY) {
        int n = (MATE - score + 1) / 2;
        return 100000L + (n < 1 ? 1 : n);
    }
    if (score <= -(MATE - MAXPLY)) {
        int n = (MATE + score + 1) / 2;
        return -(100000L + (n < 1 ? 1 : n));
    }
    return score;
}

/* print one move in coordinate notation (e2e4 / e7e8q) */
static void print_move(unsigned int m) {
    static const char pn[] = " PNBRQK";
    int f = mfrom(m), t = mto(m);
    printf("%c%c%c%c", 'a' + (f & 7), '1' + (f >> 4), 'a' + (t & 7), '1' + (t >> 4));
    if (ispromo(m)) printf("%c", pn[mfl(m) + 1]);
    printf(" ");
}

/* iterative deepening root search; returns best move (0 if aborted before any depth) */
unsigned int think(Pos *p, int maxdepth) {
    int d, i;
    unsigned int bestm = 0;
    long t0 = (long)clock();
    dbgf("think begin maxdepth=%d deadline=%ld\n", maxdepth, deadline);
    root_n = gen_moves(p, root_m);
    for (i = 0; i < root_n; i++) root_score[i] = 0;
    if (nnue_ensure_default()) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        Score alpha = -INF, beta = INF, bsc = -INF;
        unsigned int bm = 0;
        pv_len[0] = 0;
        if (deadline > 0 && (long)clock() >= deadline) break;
        if (vtime_mode && vclock_budget_hit()) break;
        nodes_search = 0;
        stop_now = 0;
        rep_n = 0;
        sort_root();
        for (i = 0; i < root_n; i++) {
            Undo u;
            int us;
            Score score;
            do_make(p, root_m[i], &u);
            us = p->side ^ 1;
            if (!is_attacked(p, p->ks[us], p->side)) {
                int pc = p->board[mfrom(root_m[i])];   /* moving piece, before the make */
                int is_cap = (u.cap != EMPTY) || (mfl(root_m[i]) == MF_EP);
                int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                root_score[i] = score;
                if (score > bsc) {
                    bsc = score;
                    bm = root_m[i];
                    pv[0][0] = root_m[i];              /* root move + the child line */
                    {
                        int pl = pv_len[1], k;
                        for (k = 0; k < pl && k < MAXPLY - 1; k++) pv[0][k + 1] = pv[1][k];
                        pv_len[0] = (pl >= MAXPLY) ? MAXPLY : pl + 1;
                    }
                }
                if (score > alpha) alpha = score;
                undo_move(p, root_m[i], &u);
                if (alpha >= beta) break;
            } else undo_move(p, root_m[i], &u);
            if (stop_now) break;
        }
        if (stop_now) break;
        bestm = bm;
        if (post_on) {
            /* CECP §10 thinking output: ply score time(cs) nodes [*seldepth *speed *tbhits] pv.
               The optional ints are parsed seldepth speed tbhits (last = tbhits), so emit a
               0 tbhits to keep speed from being misread. */
            long cs = (long)((clock() - t0) / (CLOCKS_PER_SEC / 100));
            long nps = (cs > 0) ? (nodes_search / cs * 100 + (nodes_search % cs) * 100 / cs) : 0;
            printf("%d %ld %ld %ld %d %ld 0\t", d, score_to_cecp(bsc), cs, nodes_search, d, nps);
            for (i = 0; i < pv_len[0]; i++) print_move(pv[0][i]);
            printf("\n");
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
#ifndef VCLOCK
    clock_t b0, b1;
    double bsecs;
#endif

    if (depth < 1) depth = BENCH_DEPTH;
    if (depth > 20) depth = 20;
    deadline = 0;                                /* keep the search timing-independent */
    stop_now = 0;
#ifdef VCLOCK
    vclock_reset();                              /* zero the weighted counters for the whole suite */
#endif

#ifndef VCLOCK
    b0 = clock();
#endif
    for (i = 0; i < BENCH_N; i++) {
        Pos p;
        long pos_nodes = 0;
        Score bestscore = -INF;
        int bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        parse_fen(&p, bench_fens[i]);
        g_sigs_n = 0;                            /* no game-history repetitions */
        memset(killers, 0, sizeof killers);
        root_n = gen_moves(&p, root_m);
        for (k = 0; k < root_n; k++) root_score[k] = 0;
        if (nnue_ensure_default()) { nnue_reset(&p); nnue_active = 1; }

        t0 = clock();
        for (d = 1; d <= depth; d++) {
            Score alpha = -INF, beta = INF, bsc = -INF;
            unsigned int bm = 0;
            nodes_search = 0;
            rep_n = 0;
            stop_now = 0;
            sort_root();
            for (k = 0; k < root_n; k++) {
                Undo u;
                int us;
                Score score;
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
#ifndef VCLOCK
    b1 = clock();
    bsecs = (double)(b1 - b0) / (double)CLOCKS_PER_SEC;
#endif

    /* these two lines must stay the last output: OpenBench matches them
       scanning up from the bottom of stdout */
#ifdef VCLOCK
    printf("NPS is the weighted-model estimate x1000 for OpenBench's X.XX million display\n");
#endif
    printf("\nNodes searched : %ld\n", total_nodes);
#ifdef VCLOCK
    /* nps = what the weighted model predicts on the modeled CPU (default
       80286 @ 25 MHz); the host's real nps is irrelevant to the 16-bit target.
       x1000 so OpenBench displays it as X.XX million. */
    printf("NPS: %ld\n", vclock_est_nps(total_nodes) * 1000);
#else
    printf("NPS: %ld\n", (long)(total_nodes / (bsecs > 0.0 ? bsecs : 1.0)));
#endif
    return 0;
}

#ifdef PROFILE
/* ------------------------------------------------------------------ */
/* profile: same 8-position suite as bench(), but resets the call/     */
/* feature counters first and reports a cost breakdown after. Depth    */
/* defaults to 4; identical search semantics to bench() so node counts */
/* stay the same (1,582,816 at depth 4). Builds need -DPROFILE.        */
/* ------------------------------------------------------------------ */
int profile(int depth) {
    int i, d, k;
    long total_nodes = 0;
    clock_t b0, b1;
    double bsecs;

    if (depth < 1) depth = BENCH_DEPTH;
    if (depth > 20) depth = 20;
    deadline = 0;                                /* keep the search timing-independent */
    stop_now = 0;

    c_anodes = c_qnodes = c_nextmove = 0;
    c_make = c_undo = c_gen_moves = c_gen_caps = c_gen_quiets = 0;
    c_nn_make = c_nn_undo = c_nn_eval = c_refresh = c_flip = 0;
    c_isattacked = 0;
    c_possig = 0;

    b0 = clock();
    for (i = 0; i < BENCH_N; i++) {
        Pos p;
        long pos_nodes = 0;
        long s_an = c_anodes, s_qn = c_qnodes, s_nm = c_nextmove;
        long s_mk = c_make, s_uc = c_undo, s_gm = c_gen_moves, s_gc = c_gen_caps, s_gq = c_gen_quiets;
        long s_nn = c_nn_make, s_nu = c_nn_undo, s_ev = c_nn_eval, s_rf = c_refresh;
        long s_at = c_isattacked, s_ps = c_possig;

        parse_fen(&p, bench_fens[i]);
        g_sigs_n = 0;                            /* no game-history repetitions */
        memset(killers, 0, sizeof killers);
        root_n = gen_moves(&p, root_m);
        for (k = 0; k < root_n; k++) root_score[k] = 0;
        if (nnue_ensure_default()) { nnue_reset(&p); nnue_active = 1; }

        for (d = 1; d <= depth; d++) {
            Score alpha = -INF, beta = INF;
            nodes_search = 0;
            rep_n = 0;
            stop_now = 0;
            sort_root();
            for (k = 0; k < root_n; k++) {
                Undo u;
                int us;
                Score score;
                do_make(&p, root_m[k], &u);
                us = p.side ^ 1;
                if (!is_attacked(&p, p.ks[us], p.side)) {
                    int pc = p.board[mfrom(root_m[k])];
                    int is_cap = (u.cap != EMPTY) || (mfl(root_m[k]) == MF_EP);
                    int child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                    score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                    root_score[k] = score;
                    if (score > alpha) alpha = score;
                    undo_move(&p, root_m[k], &u);
                    if (alpha >= beta) break;
                } else undo_move(&p, root_m[k], &u);
            }
            pos_nodes += nodes_search;
        }
        nnue_active = 0;
        total_nodes += pos_nodes;
        printf("pos %d: nodes=%ld an=%ld qn=%ld nx=%ld mk=%ld uc=%ld gm=%ld gc=%ld gq=%ld "
               "nm=%ld nu=%ld ev=%ld rf=%ld at=%ld ps=%ld\n",
               i + 1, pos_nodes, c_anodes - s_an, c_qnodes - s_qn, c_nextmove - s_nm,
               c_make - s_mk, c_undo - s_uc, c_gen_moves - s_gm, c_gen_caps - s_gc,
               c_gen_quiets - s_gq, c_nn_make - s_nn, c_nn_undo - s_nu, c_nn_eval - s_ev,
               c_refresh - s_rf, c_isattacked - s_at, c_possig - s_ps);
    }
    b1 = clock();
    bsecs = (double)(b1 - b0) / (double)CLOCKS_PER_SEC;

    printf("profile depth=%d nodes=%ld time=%.2fs nps=%ld\n",
           depth, total_nodes, bsecs,
           (long)(total_nodes / (bsecs > 0.0 ? bsecs : 1.0)));
    printf("profile alphabeta=%ld qsearch=%ld next_move=%ld\n",
           c_anodes, c_qnodes, c_nextmove);
    printf("profile make=%ld undo=%ld gen_moves=%ld gen_caps=%ld gen_quiets=%ld\n",
           c_make, c_undo, c_gen_moves, c_gen_caps, c_gen_quiets);
    printf("profile nn_make=%ld nn_undo=%ld nn_eval=%ld refresh_rows=%ld flips=%ld\n",
           c_nn_make, c_nn_undo, c_nn_eval, c_refresh, c_flip);
    printf("profile is_attacked=%ld pos_sig=%ld\n", c_isattacked, c_possig);
    return 0;
}
#endif /* PROFILE */

/* ------------------------------------------------------------------ */
/* search cost accounting (`chess sbench`): times the search's hot     */
/* primitives in isolation so the profile call counters can be charged. */
/* Averages over all 8 bench positions (they vary in density 5-48      */
/* moves). NNUE off. Prints ms/1000 calls; cycles/call = ms*clock_MHz.  */
/* ------------------------------------------------------------------ */
int sbench(void) {
    static Pos pos;
    static unsigned int list[256];
    MGen mg;
    Undo u;
    unsigned int m;
    int i, iters, p;
    clock_t t0, t1;
    long att = 0, caps = 0, quiets = 0, drain = 0, make10k = 0, sig = 0;

    nnue_active = 0;                                /* board-only make/undo */
    for (p = 0; p < BENCH_N; p++) {
        parse_fen(&pos, bench_fens[p]);
        gen_moves(&pos, list);

        iters = 1500;
        t0 = clock();
        for (i = 0; i < iters; i++) {
            is_attacked(&pos, pos.ks[0], 1);
            is_attacked(&pos, pos.ks[1], 0);
        }
        t1 = clock();
        att += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 300;
        t0 = clock();
        for (i = 0; i < iters; i++) gen_caps(&pos, list);
        t1 = clock();
        caps += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        t0 = clock();
        for (i = 0; i < iters; i++) gen_quiets(&pos, list);
        t1 = clock();
        quiets += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        t0 = clock();
        for (i = 0; i < iters; i++) {
            mgen_init(&pos, &mg, 0, 0, 0, 0);
            while ((m = next_move(&pos, &mg)) != 0) ;
        }
        t1 = clock();
        drain += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 2000;
        t0 = clock();
        for (i = 0; i < iters; i++) {
            do_make(&pos, list[0], &u);
            undo_move(&pos, list[0], &u);
        }
        t1 = clock();
        make10k += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 400;
        t0 = clock();
        for (i = 0; i < iters; i++) pos_sig(&pos);
        t1 = clock();
        sig += (long)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
    }

    printf("sbench att=%ld caps=%ld quiets=%ld drain=%ld make10k=%ld sig=%ld\n",
           att * 1000 / (1500 * 2 * BENCH_N), caps * 1000 / (300 * BENCH_N),
           quiets * 1000 / (300 * BENCH_N), drain * 1000 / (300 * BENCH_N),
           make10k * 1000 / (2000 * BENCH_N), sig * 1000 / (400 * BENCH_N));
    return 0;
}

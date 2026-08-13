/* search.c - alpha-beta search and iterative deepening */

#include "engine.h"

volatile i16 stop_now = 0;
i32 deadline = 0;                   /* ms deadline, 0 = no limit */

#if defined(PROFILE) || defined(VCLOCK)
i32 c_anodes = 0;                   /* alphabeta entries */
i32 c_qnodes = 0;                   /* qsearch entries */
#endif

static i32 nodes_search;
static Sig rep_path[MAX_REP_PATH];   /* position sigs along the current search line */
static i16 rep_n;

/* principal-variation lines for the `post` search-info output (CECP §10).
   pv[ply][0..pv_len[ply]-1] is the best line from ply, in move encoding. */
static u16 pv[MAXPLY + 1][MAXPLY];
static i16 pv_len[MAXPLY + 1];

static u16 killers[MAXPLY][2];          /* two killer moves per ply (quiet only) */

i16 qhist[2][6][64];   /* quiet-history: side, piece-type-1, to-square-compact */

/* bonus/penalty for a quiet move's history slot, clamped to +-QH_MAX */
static void qhist_update(Pos *p, u16 m, i16 delta) {
    i16 *h = &qhist[p->side][TY(p->board[mfrom(m)]) - 1][(m >> 6) & 0x3F];
    i16 v = *h + delta;
    if (v >  QH_MAX) v =  QH_MAX;
    if (v < -QH_MAX) v = -QH_MAX;
    *h = v;
}

#define MAX_QDEPTH 8    /* longest capture chain past the search leaf; guards against
                           qsearch explosions (deep recapture lines) */

/* reverse futility pruning: a shallow, non-PV node whose static eval already
   beats beta by more than a depth-scaled margin returns that eval without
   searching - even a move that loses the whole margin keeps the score above
   beta. RFP_DEPTH caps the window; RFP_MARGIN is the centipawn slack per ply
   (the deeper the node, the wider the margin it needs to be "safe"). */
#define RFP_DEPTH  7
#define RFP_MARGIN 100

/* root move list + scores, reused across iterative-deepening iterations */
static u16 root_m[256];
static Score root_score[256];
static i16 root_n;

/* selection sort the root moves by last iteration's score (best first) */
static void sort_root(void) {
    i16 i, j, best;
    for (i = 0; i < root_n - 1; i++) {
        best = i;
        for (j = i + 1; j < root_n; j++)
            if (root_score[j] > root_score[best]) best = j;
        if (best != i) {
            u16 tm = root_m[i]; Score ts = root_score[i];
            root_m[i] = root_m[best]; root_m[best] = tm;
            root_score[i] = root_score[best]; root_score[best] = ts;
        }
    }
}

/* ------------------------------------------------------------------ */
/* alpha-beta search                                                  */
/* ------------------------------------------------------------------ */

static Score qsearch(Pos *p, Score alpha, Score beta, i16 ply, i16 half, i16 qd);

static Score alphabeta(Pos *p, i16 depth, Score alpha, Score beta, i16 ply, i16 half) {
    MGen mg;
    u16 m, ttm = 0, bestmove = 0;
    Score best = -INF;
    i16 legal = 0, in_check = 0;

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
        Sig sig = pos_sig(p);
        i16 prior = 0;
        i16 i;
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

    /* transposition table. The probe is shared between the move-ordering use
       (the stored move, if its from-square still holds a piece of the side to
       move, is tried first in the MG_TT stage) and the cutoff use. A stored
       result with depth >= this node's depth is trusted ONLY at a non-PV node
       (zero window: beta == alpha+1): EXACT returns the score outright, and a
       matching LOWER/UPPER bound cuts off against beta/alpha. PV nodes are
       exempt so a bound never truncates the principal variation's exact score. */
    {
        u16 tmv = 0;
        Score tsc = 0;
        i16 tfl = 0, tdep = 0;
        if (tt_probe(p, ply, &tmv, &tsc, &tfl, &tdep)) {
            if (tmv && CO(p->board[mfrom(tmv)]) == (p->side ? 8 : 0))
                ttm = tmv;
            if (beta - alpha == 1 && tdep >= depth) {
                if (tfl == TT_EXACT) { rep_n--; return tsc; }
                if (tfl == TT_LOWER && tsc >= beta) { rep_n--; return tsc; }
                if (tfl == TT_UPPER && tsc <= alpha) { rep_n--; return tsc; }
            }
        }
    }

    /* in-check status of the side to move: a non-king, non-EP move from a
       square NOT on the king's rank/file/diagonal is then always legal (it can
       neither leave a check unresolved nor open a new line), so its legality
       is_attacked test can be skipped. */
    in_check = is_attacked(p, p->ks[p->side], p->side ^ 1);

    /* reverse futility pruning. Skipped while in check (the eval is unreliable
       with the king exposed), at PV nodes (their score becomes a true bound),
       and at depth 0 (qsearch already stand-pats the leaf). On a hit, pop this
       node's rep-path entry (pushed above) before returning. */
    if (depth >= 1 && depth <= RFP_DEPTH && !in_check && beta - alpha == 1) {
        Score eval = evaluate(p);
        if (eval - depth * RFP_MARGIN >= beta) {
            rep_n--;
            return eval;
        }
    }

    mgen_init(p, &mg, ply, killers[ply][0], killers[ply][1], ttm);

    {
        i16 first = 1;                     /* PVS: first legal move gets the full window */
        while ((m = next_move(p, &mg)) != 0) {
            Undo u;
            i16 us, pc, is_cap, child_half, legal_move = 0;
            Score score, alpha0 = alpha;         /* window this move is searched against */
            pc = p->board[mfrom(m)];                 /* moving piece, before the make */
            if (!pc || CO(pc) != (p->side ? 8 : 0)) continue;  /* not our piece: skip
                                                                  (guards a bogus TT move) */
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
                if (first) {
                    /* principal variation move: full window */
                    first = 0;
                    if (depth <= 0) score = -qsearch(p, -beta, -alpha, ply + 1, child_half, MAX_QDEPTH);
                    else score = -alphabeta(p, depth - 1, -beta, -alpha, ply + 1, child_half);
                } else {
                    /* PVS: zero-window search; re-search full window only if it beats alpha.
                       score < beta avoids a wasted re-search on an already-proven cutoff. */
                    if (depth <= 0) score = -qsearch(p, -alpha - 1, -alpha, ply + 1, child_half, MAX_QDEPTH);
                    else score = -alphabeta(p, depth - 1, -alpha - 1, -alpha, ply + 1, child_half);
                    if (score > alpha && score < beta) {
                        if (depth <= 0) score = -qsearch(p, -beta, -alpha, ply + 1, child_half, MAX_QDEPTH);
                        else score = -alphabeta(p, depth - 1, -beta, -alpha, ply + 1, child_half);
                    }
                }
                if (score > best) {
                    best = score;
                    bestmove = m;
                    if (ply < MAXPLY) {                  /* extend this node's PV with the child's */
                        i16 pl = pv_len[ply + 1], k;
                        pv[ply][0] = m;
                        for (k = 0; k < pl && k < MAXPLY - 1; k++) pv[ply][k + 1] = pv[ply + 1][k];
                        pv_len[ply] = (pl >= MAXPLY) ? MAXPLY : pl + 1;
                    }
                }
                if (best > alpha) alpha = best;
                undo_move(p, m, &u);
                if (alpha >= beta) {
                    /* killer + history: a true quiet (no promo/ep/castle flag, empty
                       target) that caused the cutoff gets a depth^2 bonus; the deeper
                       the cutoff, the more reliable the move, so it dominates. */
                    if (mfl(m) == 0 && u.cap == EMPTY) {
                        i16 dd = depth * depth;
                        killers[ply][1] = killers[ply][0];
                        killers[ply][0] = m;
                        qhist_update(p, m, dd);
                    }
                    break;                            /* beta cutoff */
                } else if (score <= alpha0) {
                    /* fail low: a quiet that failed to beat its window is penalized
                       (symmetric depth^2), so it sorts behind unsearched quiets. */
                    if (mfl(m) == 0 && u.cap == EMPTY)
                        qhist_update(p, m, -(i16)(depth * depth));
                }
            } else {
                undo_move(p, m, &u);
            }
        }
    }

    if (rep_n > 0) rep_n--;                  /* pop this node */
    if (!legal) {
        /* mate scores: -(MATE - ply) so the root prefers the SHORTEST mate */
        Score r = is_attacked(p, p->ks[p->side], p->side ^ 1) ? (Score)-(MATE - ply) : 0;
        if (!stop_now) tt_store(p, 0, depth, r, TT_EXACT, ply);
        return r;
    }
    if (!stop_now) {
        i16 flag = (best <= alpha) ? TT_UPPER : (best >= beta) ? TT_LOWER : TT_EXACT;
        tt_store(p, bestmove, depth, best, flag, ply);
    }
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
static Score qsearch(Pos *p, Score alpha, Score beta, i16 ply, i16 half, i16 qd) {
    MGen mg;
    u16 m;
    i16 in_check, legal = 0;
    Score stand, best = -INF;    /* fail-soft: return the true best found, even
                                    outside [alpha,beta] - gives the caller a
                                    tighter bound than a fail-hard clamp would. */

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
        best = stand;                            /* fail-soft baseline = stand-pat */
        if (stand >= beta) return stand;         /* stand-pat cutoff */
        if (stand > alpha) alpha = stand;
    }

    if (in_check) mgen_init(p, &mg, ply, 0, 0, 0);  /* all legal evasions */
    else          mgen_init_q(p, &mg, ply);          /* captures only, MVV-LVA */

    while ((m = next_move(p, &mg)) != 0) {
        Undo u;
        i16 us, pc, is_cap, child_half, legal_move = 0;
        Score score;
        pc = p->board[mfrom(m)];
        if (!pc || CO(pc) != (p->side ? 8 : 0)) continue;  /* not our piece: skip */
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
            if (score > best) best = score;      /* fail-soft: track the best move */
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
    return best;                                 /* fail-soft */
}

void search_root(Pos *p, i16 maxdepth) {
    i16 d, i;
    root_n = gen_moves(p, root_m);
    for (i = 0; i < root_n; i++) root_score[i] = 0;
    if (nnue_ensure_default()) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        Score alpha = -INF, beta = INF;
        Score bestscore = -INF;
        i16 bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        nodes_search = 0;
        t0 = clock();
        rep_n = 0;
        sort_root();
        {
            i16 first = 1;                 /* PVS: first root move gets the full window */
            for (i = 0; i < root_n; i++) {
                Undo u;
                i16 us;
                Score score;
                do_make(p, root_m[i], &u);
                us = p->side ^ 1;
                if (!is_attacked(p, p->ks[us], p->side)) {
                    i16 pc = p->board[mfrom(root_m[i])];   /* moving piece, before the make */
                    i16 is_cap = (u.cap != EMPTY) || (mfl(root_m[i]) == MF_EP);
                    i16 child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                    if (first) {
                        first = 0;
                        score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                    } else {
                        score = -alphabeta(p, d - 1, -alpha - 1, -alpha, 1, child_half);
                        if (score > alpha && score < beta)
                            score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                    }
                    root_score[i] = score;
                    if (score > bestscore) { bestscore = score; bf = mfrom(root_m[i]); bt = mto(root_m[i]); }
                    if (score > alpha) alpha = score;
                    undo_move(p, root_m[i], &u);
                    if (alpha >= beta) break;
                } else {
                    undo_move(p, root_m[i], &u);
                }
            }
        }
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        printf("depth %2d  score %5d  move %02X%02X  %8ld nodes  %7.2fs\n",
               d, bestscore, bf, bt, (long)nodes_search, secs);
    }
    nnue_active = 0;
}

/* CECP §10 thinking-output score: normal centipawns, but mate scores are
   reported as 100000+N ("mate in N moves") / -100000-N ("mated in N moves").
   The engine's internal mate score is MATE-ply (ply = distance from root to
   the terminal position), so N = (ply+1)/2. */
static i32 score_to_cecp(Score score) {
    if (score >= MATE - MAXPLY) {
        i16 n = (MATE - score + 1) / 2;
        return 100000L + (n < 1 ? 1 : n);
    }
    if (score <= -(MATE - MAXPLY)) {
        i16 n = (MATE + score + 1) / 2;
        return -(100000L + (n < 1 ? 1 : n));
    }
    return score;
}

/* print one move in coordinate notation (e2e4 / e7e8q) */
static void print_move(u16 m) {
    static const char pn[] = " PNBRQK";
    i16 f = mfrom(m), t = mto(m);
    printf("%c%c%c%c", 'a' + (f & 7), '1' + (f >> 4), 'a' + (t & 7), '1' + (t >> 4));
    if (ispromo(m)) printf("%c", pn[mfl(m) + 1]);
    printf(" ");
}

/* iterative deepening root search; returns best move (0 if aborted before any depth) */
u16 think(Pos *p, i16 maxdepth) {
    i16 d, i;
    u16 bestm = 0;
    i32 t0 = (i32)clock();
    dbgf("think begin maxdepth=%d deadline=%ld\n", maxdepth, (long)deadline);
    root_n = gen_moves(p, root_m);
    for (i = 0; i < root_n; i++) root_score[i] = 0;
    if (nnue_ensure_default()) { nnue_reset(p); nnue_active = 1; }
    for (d = 1; d <= maxdepth; d++) {
        Score alpha = -INF, beta = INF, bsc = -INF;
        u16 bm = 0;
        pv_len[0] = 0;
        if (deadline > 0 && (long)clock() >= deadline) break;
        if (vtime_mode && vclock_budget_hit()) break;
        nodes_search = 0;
        stop_now = 0;
        rep_n = 0;
        sort_root();
        {
            i16 first = 1;                 /* PVS: first root move gets the full window */
            for (i = 0; i < root_n; i++) {
                Undo u;
                i16 us;
                Score score;
                do_make(p, root_m[i], &u);
                us = p->side ^ 1;
                if (!is_attacked(p, p->ks[us], p->side)) {
                    i16 pc = p->board[mfrom(root_m[i])];   /* moving piece, before the make */
                    i16 is_cap = (u.cap != EMPTY) || (mfl(root_m[i]) == MF_EP);
                    i16 child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                    if (first) {
                        first = 0;
                        score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                    } else {
                        score = -alphabeta(p, d - 1, -alpha - 1, -alpha, 1, child_half);
                        if (score > alpha && score < beta)
                            score = -alphabeta(p, d - 1, -beta, -alpha, 1, child_half);
                    }
                    root_score[i] = score;
                    if (score > bsc) {
                        bsc = score;
                        bm = root_m[i];
                        pv[0][0] = root_m[i];              /* root move + the child line */
                        {
                            i16 pl = pv_len[1], k;
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
        }
        if (stop_now) break;
        bestm = bm;
        if (post_on) {
            /* CECP §10 thinking output: ply score time(cs) nodes [*seldepth *speed *tbhits] pv.
               The optional ints are parsed seldepth speed tbhits (last = tbhits), so emit a
               0 tbhits to keep speed from being misread. */
            i32 cs = (i32)((clock() - t0) / (CLOCKS_PER_SEC / 100));
            i32 nps = (cs > 0) ? (nodes_search / cs * 100 + (nodes_search % cs) * 100 / cs) : 0;
            printf("%d %ld %ld %ld %d %ld 0\t", d, (long)score_to_cecp(bsc), (long)cs,
                   (long)nodes_search, d, (long)nps);
            for (i = 0; i < pv_len[0]; i++) print_move(pv[0][i]);
            printf("\n");
            fflush(stdout);
        }
    }
    dbgf("think end bestm=%04X stop=%d d=%d\n", (unsigned)bestm, (int)stop_now, d - 1);
    /* safety net: bestm must be one of the generated root moves. A corrupt
       search/TT state must never leak an illegal move to the GUI; fall back
       to the first legal root move and log it. */
    {
        i16 k2, have = 0;
        for (k2 = 0; k2 < root_n; k2++)
            if (root_m[k2] == bestm) { have = 1; break; }
        if (!have) {
            dbgf("think bestm=%04X NOT a root move - fallback to root_m[0]\n", (unsigned)bestm);
            bestm = (root_n > 0) ? root_m[0] : 0;
        }
    }
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
    i16 i, d, k;
    i32 total_nodes = 0;
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
        i32 pos_nodes = 0;
        Score bestscore = -INF;
        i16 bf = 0, bt = 0;
        clock_t t0, t1;
        double secs;

        parse_fen(&p, bench_fens[i]);
        g_sigs_n = 0;                            /* no game-history repetitions */
        memset(killers, 0, sizeof killers);
        memset(qhist, 0, sizeof qhist);
        tt_clear();                              /* no cross-position TT reuse */
        root_n = gen_moves(&p, root_m);
        for (k = 0; k < root_n; k++) root_score[k] = 0;
        if (nnue_ensure_default()) { nnue_reset(&p); nnue_active = 1; }

        t0 = clock();
        for (d = 1; d <= depth; d++) {
            Score alpha = -INF, beta = INF, bsc = -INF;
            u16 bm = 0;
            nodes_search = 0;
            rep_n = 0;
            stop_now = 0;
            sort_root();
            {
                i16 first = 1;                 /* PVS: first root move gets the full window */
                for (k = 0; k < root_n; k++) {
                    Undo u;
                    i16 us;
                    Score score;
                    do_make(&p, root_m[k], &u);
                    us = p.side ^ 1;
                    if (!is_attacked(&p, p.ks[us], p.side)) {
                        i16 pc = p.board[mfrom(root_m[k])];
                        i16 is_cap = (u.cap != EMPTY) || (mfl(root_m[k]) == MF_EP);
                        i16 child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                        if (first) {
                            first = 0;
                            score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                        } else {
                            score = -alphabeta(&p, d - 1, -alpha - 1, -alpha, 1, child_half);
                            if (score > alpha && score < beta)
                                score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                        }
                        root_score[k] = score;
                        if (score > bsc) { bsc = score; bm = root_m[k]; }
                        if (score > alpha) alpha = score;
                        undo_move(&p, root_m[k], &u);
                        if (alpha >= beta) break;
                    } else undo_move(&p, root_m[k], &u);
                }
            }
            pos_nodes += nodes_search;
            if (d == depth) { bestscore = bsc; bf = mfrom(bm); bt = mto(bm); }
        }
        nnue_active = 0;
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        total_nodes += pos_nodes;
        printf("position %2d/%d  depth %2d  score %5d  move %02X%02X  n=%10ld  t=%7.2fs\n",
               i + 1, BENCH_N, depth, bestscore, bf, bt, (long)pos_nodes, secs);
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
    printf("\nNodes searched : %ld\n", (long)total_nodes);
#ifdef VCLOCK
    /* nps = what the weighted model predicts on the modeled CPU (default
       80286 @ 25 MHz); the host's real nps is irrelevant to the 16-bit target.
       x1000 so OpenBench displays it as X.XX million. */
    printf("NPS: %ld\n", (long)(vclock_est_nps(total_nodes) * 1000));
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
/* stay the same (911,306 at depth 4 with the TT). Builds need -DPROFILE. */
/* ------------------------------------------------------------------ */
int profile(int depth) {
    i16 i, d, k;
    i32 total_nodes = 0;
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
    c_tt_probe = 0;
    c_tt_store = 0;

    b0 = clock();
    for (i = 0; i < BENCH_N; i++) {
        Pos p;
        i32 pos_nodes = 0;
        i32 s_an = c_anodes, s_qn = c_qnodes, s_nm = c_nextmove;
        i32 s_mk = c_make, s_uc = c_undo, s_gm = c_gen_moves, s_gc = c_gen_caps, s_gq = c_gen_quiets;
        i32 s_nn = c_nn_make, s_nu = c_nn_undo, s_ev = c_nn_eval, s_rf = c_refresh;
        i32 s_at = c_isattacked, s_ps = c_possig;
        i32 s_tp = c_tt_probe, s_ts = c_tt_store;

        parse_fen(&p, bench_fens[i]);
        g_sigs_n = 0;                            /* no game-history repetitions */
        memset(killers, 0, sizeof killers);
        memset(qhist, 0, sizeof qhist);
        tt_clear();                              /* no cross-position TT reuse */
        root_n = gen_moves(&p, root_m);
        for (k = 0; k < root_n; k++) root_score[k] = 0;
        if (nnue_ensure_default()) { nnue_reset(&p); nnue_active = 1; }

        for (d = 1; d <= depth; d++) {
            Score alpha = -INF, beta = INF;
            nodes_search = 0;
            rep_n = 0;
            stop_now = 0;
            sort_root();
            {
                i16 first = 1;                 /* PVS: first root move gets the full window */
                for (k = 0; k < root_n; k++) {
                    Undo u;
                    i16 us;
                    Score score;
                    do_make(&p, root_m[k], &u);
                    us = p.side ^ 1;
                    if (!is_attacked(&p, p.ks[us], p.side)) {
                        i16 pc = p.board[mfrom(root_m[k])];
                        i16 is_cap = (u.cap != EMPTY) || (mfl(root_m[k]) == MF_EP);
                        i16 child_half = (TY(pc) == 1 || is_cap) ? 0 : g_half + 1;
                        if (first) {
                            first = 0;
                            score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                        } else {
                            score = -alphabeta(&p, d - 1, -alpha - 1, -alpha, 1, child_half);
                            if (score > alpha && score < beta)
                                score = -alphabeta(&p, d - 1, -beta, -alpha, 1, child_half);
                        }
                        root_score[k] = score;
                        if (score > alpha) alpha = score;
                        undo_move(&p, root_m[k], &u);
                        if (alpha >= beta) break;
                    } else undo_move(&p, root_m[k], &u);
                }
            }
            pos_nodes += nodes_search;
        }
        nnue_active = 0;
        total_nodes += pos_nodes;
        printf("pos %d: nodes=%ld an=%ld qn=%ld nx=%ld mk=%ld uc=%ld gm=%ld gc=%ld gq=%ld "
               "nm=%ld nu=%ld ev=%ld rf=%ld at=%ld ps=%ld tp=%ld ts=%ld\n",
               i + 1, (long)pos_nodes, (long)(c_anodes - s_an), (long)(c_qnodes - s_qn),
               (long)(c_nextmove - s_nm), (long)(c_make - s_mk), (long)(c_undo - s_uc),
               (long)(c_gen_moves - s_gm), (long)(c_gen_caps - s_gc), (long)(c_gen_quiets - s_gq),
               (long)(c_nn_make - s_nn), (long)(c_nn_undo - s_nu), (long)(c_nn_eval - s_ev),
               (long)(c_refresh - s_rf), (long)(c_isattacked - s_at), (long)(c_possig - s_ps),
               (long)(c_tt_probe - s_tp), (long)(c_tt_store - s_ts));
    }
    b1 = clock();
    bsecs = (double)(b1 - b0) / (double)CLOCKS_PER_SEC;

    printf("profile depth=%d nodes=%ld time=%.2fs nps=%ld\n",
           depth, (long)total_nodes, bsecs,
           (long)(total_nodes / (bsecs > 0.0 ? bsecs : 1.0)));
    printf("profile alphabeta=%ld qsearch=%ld next_move=%ld\n",
           (long)c_anodes, (long)c_qnodes, (long)c_nextmove);
    printf("profile make=%ld undo=%ld gen_moves=%ld gen_caps=%ld gen_quiets=%ld\n",
           (long)c_make, (long)c_undo, (long)c_gen_moves, (long)c_gen_caps, (long)c_gen_quiets);
    printf("profile nn_make=%ld nn_undo=%ld nn_eval=%ld refresh_rows=%ld flips=%ld\n",
           (long)c_nn_make, (long)c_nn_undo, (long)c_nn_eval, (long)c_refresh, (long)c_flip);
    printf("profile is_attacked=%ld pos_sig=%ld\n", (long)c_isattacked, (long)c_possig);
    printf("profile tt_probe=%ld tt_store=%ld\n", (long)c_tt_probe, (long)c_tt_store);
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
    static u16 list[256];
    MGen mg;
    Undo u;
    u16 m;
    i16 i, iters, p;
    clock_t t0, t1;
    i32 att = 0, caps = 0, quiets = 0, drain = 0, make10k = 0, sig = 0;
    i32 ttpr = 0, ttst = 0;
    u16 mv;
    Score tsc;
    i16 tfl, tdp;

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
        att += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 300;
        t0 = clock();
        for (i = 0; i < iters; i++) gen_caps(&pos, list);
        t1 = clock();
        caps += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        t0 = clock();
        for (i = 0; i < iters; i++) gen_quiets(&pos, list);
        t1 = clock();
        quiets += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        t0 = clock();
        for (i = 0; i < iters; i++) {
            mgen_init(&pos, &mg, 0, 0, 0, 0);
            while ((m = next_move(&pos, &mg)) != 0) ;
        }
        t1 = clock();
        drain += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 2000;
        t0 = clock();
        for (i = 0; i < iters; i++) {
            do_make(&pos, list[0], &u);
            undo_move(&pos, list[0], &u);
        }
        t1 = clock();
        make10k += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        iters = 400;
        t0 = clock();
        for (i = 0; i < iters; i++) pos_sig(&pos);
        t1 = clock();
        sig += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

        /* TT probe (hit path) + store, averaged over the 8 positions */
        iters = 2000;
        tt_clear();
        tt_store(&pos, list[0], 5, 100, TT_EXACT, 0);
        t0 = clock();
        for (i = 0; i < iters; i++) tt_probe(&pos, 0, &mv, &tsc, &tfl, &tdp);
        t1 = clock();
        ttpr += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
        t0 = clock();
        for (i = 0; i < iters; i++) tt_store(&pos, list[0], 5, 100, TT_EXACT, 0);
        t1 = clock();
        ttst += (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
    }

    printf("sbench att=%ld caps=%ld quiets=%ld drain=%ld make10k=%ld sig=%ld "
           "ttprobe=%ld ttstore=%ld\n",
           (long)(att * 1000 / (1500 * 2 * BENCH_N)), (long)(caps * 1000 / (300 * BENCH_N)),
           (long)(quiets * 1000 / (300 * BENCH_N)), (long)(drain * 1000 / (300 * BENCH_N)),
           (long)(make10k * 1000 / (2000 * BENCH_N)), (long)(sig * 1000 / (400 * BENCH_N)),
           (long)(ttpr * 1000 / (2000 * BENCH_N)), (long)(ttst * 1000 / (2000 * BENCH_N)));
    return 0;
}

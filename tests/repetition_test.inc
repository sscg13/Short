/* Slow legal-move oracle for the root-aware twofold search policy. */
static i16 rt_fails, rt_cases, rt_hits;
static i32 rt_move_checks;
static u16 rt_moves[256];
static i16 rt_oracle(Pos *p, i16 half, i16 ply) {
    i16 n = gen_moves(p, rt_moves), k, i, count, us = p->side;
    i16 current = g_sigs_n + rep_n - 1, first = rep_begin(half, current);
    for (k = 0; k < n; ++k) {
        Undo u;
        Sig target;
        i16 reversible, pc = p->board[mfrom(rt_moves[k])];
        do_make(p, rt_moves[k], &u);
        target = p->sig;
        reversible = TY(pc) != 1 && !u.cap && !mfl(rt_moves[k]) &&
                     u.castle == p->castle && u.ep == p->ep &&
                     !is_attacked(p, p->ks[us], p->side);
        count = 0;
        if (!is_attacked(p, p->ks[us], p->side))
            for (i = current - 1; i >= first; i -= 2)
                if (rep_at(i) == p->sig) {
                    if (i > current - ply) count += 2;
                    else ++count;
                }
        undo_move(p, rt_moves[k], &u);
        if (reversible) {
            ++rt_move_checks;
            if (repetition_move(p, target) != rt_moves[k]) ++rt_fails;
        }
        if (count >= 2) return 1;
    }
    return 0;
}
static void rt_check(Pos *p, i16 half, i16 ply, i16 want, const char *label) {
    Sig saved = p->sig;
    i16 fast, slow;
    fast = rep_upcoming(p, half, ply); slow = rt_oracle(p, half, ply);
    ++rt_cases; rt_hits += fast;
    if (fast != slow || (want >= 0 && fast != want) || p->sig != saved) {
        ++rt_fails;
        printf("FAIL %s fast=%d slow=%d want=%d\n", label, fast, slow, want);
    }
}
static void rt_target(const char *fen, const char *target_fen, i16 want, const char *label) {
    Pos p, target;
    i16 i, split;
    Sig history[7];
    parse_fen(&p, fen); parse_fen(&target, target_fen);
    for (i = 0; i < 7; ++i) g_sigs[i] = (Sig)(100 + i);
    g_sigs[0] = target.sig; g_sigs[4] = target.sig; g_sigs_n = 7;
    for (i = 0; i < 7; ++i) history[i] = g_sigs[i];
    for (split = 0; split <= 7; ++split) {
        Pos fake;
        for (i = 0; i < split; ++i) g_sigs[i] = history[i];
        g_sigs_n = split; rep_reset();
        for (i = split; i < 7; ++i) { fake.sig = history[i]; rep_enter(&fake, i, 0); }
        rep_enter(&p, 7, 0);
        rt_check(&p, 7, 7-split, want, "game/search history split");
        rt_check(&p, 6, 7-split, want && split < 4, "irreversible history boundary");
    }
    /* Destination at index 4: before root (ply 2), at root (3),
       strictly after root (4). Only the last permits twofold. */
    for (i = 0; i < 7; ++i) g_sigs[i] = history[i];
    g_sigs[0] = (Sig)100; g_sigs_n = 7;
    rep_reset(); rep_enter(&p, 7, 0);
    rt_check(&p, 7, 2, 0, "single occurrence before root");
    rt_check(&p, 7, 3, 0, "single occurrence at root");
    rt_check(&p, 7, 4, want, "single occurrence after root");
    rt_check(&p, 3, 4, want, "shortest upcoming cycle");
    rt_check(&p, 2, 4, 0, "zeroing move blocks cycle");
    rep_game = 0; rep_floor = 0;
    rt_check(&p, 7, 4, 0, "null blocks twofold");
    for (i = 0; i < 7; ++i) g_sigs[i] = history[i];
    g_sigs_n = 7;
    rep_reset(); rep_enter(&p, 7, 0);
    rt_check(&p, 7, 0, want, label);
    rep_game = 0; rep_floor = 0;
    rt_check(&p, 7, 0, 0, "null boundary");
    rep_reset();
    if (want) {
        deadline = 0; stop_now = 0; vclock_reset(); tt_clear();
        if (alphabeta(&p, 2, -1, 0, 1, 7, NODE_CUT) != 0 || rep_n) ++rt_fails;
    }
}
/* Replay the XBoard history contract: game history includes the root, and
   each search child is appended only on entry. Exercise both side parities. */
static void rt_search_cycle(void) {
    static const i16 from[4] = {0x06,0x76,0x25,0x55};
    static const i16 to[4] = {0x25,0x55,0x06,0x76};
    i16 black, prefix, step, i;
    for (black = 0; black < 2; ++black) for (prefix = 0; prefix <= 4; prefix += 4) {
        Pos p;
        parse_fen(&p, black ? "4k1n1/8/8/8/8/8/8/4K1N1 b - - 0 1" :
                             "4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1");
        for (i = 0; i < prefix; ++i) g_sigs[i] = (Sig)(100+i);
        g_sigs[prefix] = p.sig; g_sigs_n = prefix + 1;
        rep_reset();
        for (step = 1; step <= 5; ++step) {
            Undo u;
            i16 index = (step - 1) & 3;
            if (black) index ^= 1;
            do_make(&p, MK(to[index], from[index], 0, 0), &u);
            if (step == 5) {
                if (!rep_enter(&p, step, step) || rep_n != 4) ++rt_fails;
                break;
            }
            if (rep_enter(&p, step, step)) { ++rt_fails; break; }
            rt_check(&p, step, step, step == 4, "legal cycle below root");
            if (step == 4) {
                rep_game = 0; rep_floor = 1;
                rt_check(&p, step, step, 0, "null excludes first search child");
                rep_floor = 0;
                rt_check(&p, step, step, 1, "cycle entirely after null boundary");
                rep_game = 1;
                --rep_n;
                deadline = 0; stop_now = 0; vclock_reset(); tt_clear();
                if (alphabeta(&p, 2, -1, 0, step, step, NODE_CUT) != 0 || rep_n != 3) ++rt_fails;
                if (rep_enter(&p, step, step)) ++rt_fails;
            }
        }
    }
    g_sigs_n = 0; rep_reset();
}
/* At halfmove 99, quiet king moves draw while pawn moves can preserve the
   material advantage. Exercise both root paths and non-capturing promotions. */
static void rt_root_halfmove(const char *fen, i16 promotion) {
    Pos p, original;
    i16 path, i, pawns, quiets;
    i16 save_post = post_on, save_virtual = vtime_mode;
    i16 save_half = g_half, save_full = g_full;
    post_on = vtime_mode = 0;
    for (path = 0; path < 2; ++path) {
        u16 best = 0;
        Score best_score = -INF;
        i16 failed = 0;
        parse_fen(&p, fen); original = p;
        search_clear_ordering(); tt_clear(); vclock_reset();
        search_set_limits(0, 0); stop_now = 0;
        g_sigs_n = 1; g_sigs[0] = p.sig; rep_reset();
        if (path == 0) best = think(&p, 1);
        else search_root(&p, 1);
        pawns = quiets = 0;
        for (i = 0; i < root_n; ++i) {
            u16 m = root_m[i];
            if (TY(p.board[mfrom(m)]) == 1) {
                ++pawns;
                if ((!!ispromo(m)) != promotion) failed = 1;
            } else {
                ++quiets;
                if (root_score[i] != 0) failed = 1;
            }
            if (root_score[i] > best_score) {
                best_score = root_score[i];
                if (path) best = m;
            }
        }
        ++rt_cases;
        if (failed || !pawns || !quiets || !best || best_score <= 0 ||
            TY(p.board[mfrom(best)]) != 1 ||
            memcmp(&p, &original, sizeof p) || g_half != 99 || rep_n || nnue_active) {
            ++rt_fails;
            printf("FAIL root pawn halfmove path=%d promotion=%d\n", path, promotion);
        }
    }
    post_on = save_post; vtime_mode = save_virtual;
    g_half = save_half; g_full = save_full;
    g_sigs_n = 0; rep_reset();
}
/* En passant belongs to the position only while the side to move has a pawn
   that could capture there (FIDE 9.2.2). A double push nobody can answer must
   hash exactly like the same position reached without the target, and a target
   a pawn can use must survive with its capture generated. */
static void rt_ep_canon(void) {
    Pos p;
    Undo u;
    Sig push;
    i16 i, n, caps = 0;
    u16 moves[256];

    /* 1.e4: no black pawn on d4/f4, so the target is not part of the position. */
    parse_fen(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    do_make(&p, MK(0x34, 0x14, 0, 0), &u);            /* e2e4 */
    push = p.sig; ++rt_cases;
    if (p.ep != -1) { ++rt_fails; printf("FAIL ep stale field %d\n", p.ep); }
    parse_fen(&p, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    if (push != p.sig) { ++rt_fails; printf("FAIL ep canonical push sig\n"); }

    /* The same double push with a black pawn on e4 keeps the target... */
    parse_fen(&p, "7k/8/8/8/4p3/8/3P4/7K w - - 0 1");
    do_make(&p, MK(0x33, 0x13, 0, 0), &u);            /* d2d4 */
    ++rt_cases;
    if (p.ep != 0x23) { ++rt_fails; printf("FAIL ep target lost %d\n", p.ep); }
    n = gen_moves(&p, moves);
    for (i = 0; i < n; ++i) if (mfl(moves[i]) == MF_EP) ++caps;
    if (caps != 1) { ++rt_fails; printf("FAIL ep capture count %d\n", caps); }

    /* ...and a parsed FEN target no pawn can use is dropped, while a usable one
       survives with its capture. */
    parse_fen(&p, "7k/8/8/8/4P3/8/8/7K b - e3 0 1");
    ++rt_cases;
    if (p.ep != -1) { ++rt_fails; printf("FAIL ep parsed stale %d\n", p.ep); }
    parse_fen(&p, "7k/8/8/3Pp3/8/8/8/7K w - e6 0 1");
    ++rt_cases;
    if (p.ep != 0x54) { ++rt_fails; printf("FAIL ep parsed target %d\n", p.ep); }
    caps = 0; n = gen_moves(&p, moves);
    for (i = 0; i < n; ++i) if (mfl(moves[i]) == MF_EP) ++caps;
    if (caps != 1) { ++rt_fails; printf("FAIL ep parsed capture %d\n", caps); }
}
/* Checkmate outranks the fifty-move rule (FIDE 9.6.2): at the limit a mated
   position is still lost, while a position with an evasion, or a stalemate,
   stays drawn. Exercises the alphabeta horizon check; qsearch mirrors it. */
static void rt_fifty_mate(void) {
    Pos p;
    Score s;
    i16 i;
    static const char *fen[3] = {
        "7k/6Q1/6K1/8/8/8/8/8 b - - 100 1",    /* Qg7 mate */
        "7k/5Q2/6K1/8/8/8/8/8 b - - 100 1",    /* stalemate */
        "7k/8/6KQ/8/8/8/8/8 b - - 100 1"       /* check with Kg8 available */
    };
    static const Score want[3] = { -(MATE - 1), 0, 0 };
    nnue_active = 0;
    for (i = 0; i < 3; ++i) {
        parse_fen(&p, fen[i]);
        g_sigs_n = 0; rep_reset();
        deadline = 0; stop_now = 0; vclock_reset(); tt_clear();
        s = alphabeta(&p, 2, -INF, INF, 1, MAX_HALF, NODE_CUT);
        ++rt_cases;
        if (s != want[i] || rep_n) {
            ++rt_fails;
            printf("FAIL fifty-move case %d score=%d want=%d rep_n=%d\n",
                   i, (int)s, (int)want[i], (int)rep_n);
        }
    }
}
int repetition_selftest(void) {
    Pos p;
    u32 seed = 17;
    i16 game, step, half, n, i, found;
    static u16 moves[256];
    static const char *cycle[4] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    static const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    nnue_active = 0; g_sigs_n = 0; rep_reset();
    rt_target("k7/8/8/8/8/8/8/4K1N1 w - - 7 1",
              "k7/8/8/8/8/5N2/8/4K3 b - - 8 1", 1, "knight");
    rt_target("k3r3/8/8/8/8/8/4R3/4K3 w - - 7 1",
              "k3r3/8/8/8/8/8/3R4/4K3 b - - 8 1", 0, "pinned rook");
    rt_target("k4r2/8/8/8/8/8/8/4K3 w - - 7 1",
              "k4r2/8/8/8/8/8/8/5K2 b - - 8 1", 0, "king into check");
    rt_target("k7/8/8/8/8/8/P7/R3K3 w - - 7 1",
              "k7/8/8/8/R7/8/P7/4K3 b - - 8 1", 0, "blocked rook");
    rt_target("k7/8/8/8/8/8/8/4K2R w K - 7 1",
              "k7/8/8/8/8/8/8/5K1R b K - 8 1", 0, "castling rights");
    rt_target("4k1n1/8/8/8/8/8/8/K7 b - - 7 1",
              "4k3/8/5n2/8/8/8/8/K7 w - - 8 1", 1, "black knight");
    rt_search_cycle();
    rt_ep_canon();
    rt_fifty_mate();
    parse_fen(&p, start);
    for (i = 0; i < 4; ++i) g_sigs[i] = (Sig)(100+i);
    g_sigs[0] = p.sig; g_sigs_n = 4;
    rep_reset(); if (rep_enter(&p, 4, 4)) ++rt_fails; /* equal root */
    rep_reset(); if (rep_enter(&p, 4, 3)) ++rt_fails; /* before root */
    rep_reset(); if (!rep_enter(&p, 4, 5) || rep_n) ++rt_fails;
    rep_reset(); if (rep_enter(&p, 3, 5)) ++rt_fails; /* irreversible */
    rep_reset(); rep_game = 0; if (rep_enter(&p, 4, 5)) ++rt_fails;
    parse_fen(&p, start); g_sigs_n = 0; half = 0;
    for (step = 0; step < 8; ++step) {
        Undo u; u16 move = 0;
        rep_reset(); rep_enter(&p, half, 0); rt_check(&p, half, 0, step == 7, "real knight cycle");
        n = gen_moves(&p, moves);
        for (i = 0; i < n; ++i) {
            const char *c = cycle[step & 3];
            if (mfrom(moves[i]) == (c[0]-'a') + 16*(c[1]-'1') &&
                mto(moves[i]) == (c[2]-'a') + 16*(c[3]-'1')) move = moves[i];
        }
        if (!move) { ++rt_fails; break; }
        g_sigs[g_sigs_n++] = p.sig; do_make(&p, move, &u); ++half;
    }
    rep_reset();
    if (!rep_enter(&p, half, 0) || rep_n) ++rt_fails; /* actual third occurrence */
    /* Actual legal histories, with pawn/capture resets and both side parities. */
    for (game = 0; game < 12; ++game) {
        parse_fen(&p, start); g_sigs_n = 0; half = 0;
        for (step = 0; step < 80; ++step) {
            Undo u;
            rep_reset();
            if (rep_enter(&p, half, 0)) break;
            rt_check(&p, half, 0, -1, "legal walk");
            if (half >= 4) rt_check(&p, half, 4, -1, "legal walk search root");
            if (half >= 8) rt_check(&p, half, 8, -1, "legal walk deeper search root");
            n = gen_moves(&p, moves); found = 0;
            if (!n) break;
            seed = seed * 1664525UL + 1013904223UL;
            for (i = 0; i < n; ++i) {
                i16 index = (i + (i16)(seed % (u32)n)) % n;
                u16 m = moves[index]; i16 us = p.side, pc = p.board[mfrom(m)];
                Sig before = p.sig;
                do_make(&p, m, &u);
                if (!is_attacked(&p, p.ks[us], p.side)) {
                    g_sigs[g_sigs_n++] = before;
                    if (TY(pc) == 1 || u.cap || mfl(m) == MF_EP) { half = 0; g_sigs_n = 0; }
                    else ++half;
                    found = 1; break;
                }
                undo_move(&p, m, &u);
            }
            if (!found || g_sigs_n >= 100) break;
        }
    }
    rt_root_halfmove("7k/8/8/8/8/8/P7/K7 w - - 99 1", 0);
    rt_root_halfmove("k7/p7/8/8/8/8/8/7K b - - 99 1", 0);
    rt_root_halfmove("7k/P7/8/8/8/8/8/K7 w - - 99 1", 1);
    rt_root_halfmove("k7/8/8/8/8/8/p7/7K b - - 99 1", 1);
    g_sigs_n = 0; rep_reset();
    printf("Repetition selftest: cases=%d hits=%d moves=%ld failures=%d\n", rt_cases, rt_hits, (long)rt_move_checks, rt_fails);
    return rt_fails != 0;
}

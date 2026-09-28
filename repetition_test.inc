/* Slow legal-move oracle for the exact existing threefold policy. */
static i16 rt_fails, rt_cases, rt_hits;
static i32 rt_move_checks;
static u16 rt_moves[256];
static i16 rt_oracle(Pos *p, i16 half) {
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
                if (rep_at(i) == p->sig) ++count;
        undo_move(p, rt_moves[k], &u);
        if (reversible) {
            ++rt_move_checks;
            if (repetition_move(p, target) != rt_moves[k]) ++rt_fails;
        }
        if (count >= 2) return 1;
    }
    return 0;
}
static void rt_check(Pos *p, i16 half, i16 want, const char *label) {
    Sig saved = p->sig;
    i16 fast, slow;
    fast = rep_upcoming(p, half); slow = rt_oracle(p, half);
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
        for (i = split; i < 7; ++i) { fake.sig = history[i]; rep_enter(&fake, i); }
        rep_enter(&p, 7);
        rt_check(&p, 7, want, "game/search history split");
        rt_check(&p, 6, 0, "irreversible history boundary");
    }
    for (i = 0; i < 7; ++i) g_sigs[i] = history[i];
    g_sigs_n = 7;
    rep_reset(); rep_enter(&p, 7);
    rt_check(&p, 7, want, label);
    rep_game = 0; rep_floor = 0;
    rt_check(&p, 7, 0, "null boundary");
    rep_reset();
    if (want) {
        deadline = 0; stop_now = 0; vclock_reset(); tt_clear();
        if (qsearch(&p, -1, 0, 1, 7, 8) != 0 || rep_n) ++rt_fails;
        if (alphabeta(&p, 2, -1, 0, 1, 7) != 0 || rep_n) ++rt_fails;
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
    parse_fen(&p, start); g_sigs_n = 0; half = 0;
    for (step = 0; step < 8; ++step) {
        Undo u; u16 move = 0;
        rep_reset(); rep_enter(&p, half); rt_check(&p, half, step == 7, "real knight cycle");
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
    if (!rep_enter(&p, half) || rep_n) ++rt_fails; /* actual third occurrence */
    /* Actual legal histories, with pawn/capture resets and both side parities. */
    for (game = 0; game < 12; ++game) {
        parse_fen(&p, start); g_sigs_n = 0; half = 0;
        for (step = 0; step < 80; ++step) {
            Undo u;
            rep_reset();
            if (rep_enter(&p, half)) break;
            rt_check(&p, half, -1, "legal walk");
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
    g_sigs_n = 0; rep_reset();
    printf("Repetition selftest: cases=%d hits=%d moves=%ld failures=%d\n", rt_cases, rt_hits, (long)rt_move_checks, rt_fails);
    return rt_fails != 0;
}

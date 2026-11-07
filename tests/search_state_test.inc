/* SEARCH_TEST only: null-state round trips and killer move-set regressions.
   Static scratch keeps diagnostic buffers off the DOS search stack. */
static i16 st_checks, st_fails;
static Pos st_hash_pos;
static u16 st_all[256], st_got[256];

static void st_check(i16 ok, const char *label) {
    st_checks++;
    if (!ok) { st_fails++; printf("searchtest FAIL: %s\n", label); }
}

static i16 st_same(const Pos *a, const Pos *b) {
    return !memcmp(a->board, b->board, sizeof a->board) &&
           a->side == b->side && a->castle == b->castle && a->ep == b->ep &&
           a->ks[0] == b->ks[0] && a->ks[1] == b->ks[1] && a->sig == b->sig;
}

static void st_hash(Pos *p) {
    st_hash_pos = *p;
    zob_compute(&st_hash_pos);
    st_check(p->sig == st_hash_pos.sig, "incremental hash equals full hash");
}

static int search_state_selftest(void) {
    static const char *null_fens[] = {
        "rnbqkbnr/pppppppp/8/8/6P1/8/PPPPPP1P/RNBQKBNR b KQkq g3 0 1",
        "rnbqkbnr/pppppp1p/8/6p1/8/8/PPPPPPPP/RNBQKBNR w KQkq g6 0 2",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"
    };
    static const char *killer_fens[] = {
        "rnbqkbnr/ppp1pppp/8/8/4p3/3B4/PPPP1PPP/RNBQK1NR w KQkq - 0 3",
        "rnbqk1nr/pppp1ppp/3b4/4P3/8/8/PPP1PPPP/RNBQKBNR b KQkq - 0 3"
    };
    static Pos p, original, passed;
    MGen mg;
    Undo u;
    Score initial, parent_score;
    u16 candidate, m;
    i16 c, ep, nested_ep, n, i, j, k, slot, na, expected;
    st_checks = st_fails = 0;
    nnue_active = nnue_enabled;
    for (c = 0; c < 6; c++) {
        parse_fen(&p, null_fens[c]);
        original = p;
        nnue_reset(&p);
        initial = evaluate(&p);
        ep = nm_make(&p);
        st_check(ep == original.ep && p.ep == -1 && p.side == (original.side ^ 1),
                 "null expires EP and flips side");
        st_hash(&p);
        passed = p;
        parent_score = evaluate(&p);
        nested_ep = nm_make(&p);
        st_check(nested_ep == -1 && p.ep == -1 && p.side == original.side,
                 "nested null cannot revive EP");
        st_hash(&p);
        nm_undo(&p, nested_ep);
        st_check(st_same(&p, &passed), "nested null restores parent");
        n = gen_moves(&p, st_all);
        for (i = 0; i < n; i++) {
            st_check(mfl(st_all[i]) != MF_EP, "no EP move after null");
            do_make(&p, st_all[i], &u);
            st_hash(&p);
            (void)evaluate(&p);  /* materialize lazy child accumulators */
            undo_move(&p, st_all[i], &u);
            st_check(st_same(&p, &passed), "real child restores null parent");
            st_check(evaluate(&p) == parent_score, "NNUE child undo restores score");
        }
        nm_undo(&p, ep);
        st_check(st_same(&p, &original), "null undo restores complete position");
        st_hash(&p);
        st_check(evaluate(&p) == initial, "null undo restores NNUE score");
        n = gen_caps(&p, st_all);
        expected = 0;
        for (i = 0; i < n; i++) if (mfl(st_all[i]) == MF_EP) expected++;
        if (c == 2 || c == 3) st_check(expected == 1, "real EP right restored");
    }
    /* Both colours, friendly/enemy blockers, clear path, wrong start rank,
       occupied destination, and either killer slot. Compare whole move sets. */
    for (c = 0; c < 2; c++) {
        i16 from = c ? 0x63 : 0x13, fwd = c ? -16 : 16;
        i16 to = from + 2 * fwd;
        for (k = 0; k < 5; k++) {
            parse_fen(&p, killer_fens[c]);
            candidate = MK(to, from, 0, 0);
            if (k == 1) p.board[from + fwd] = c ? WB : BB;
            if (k >= 2) p.board[from + fwd] = EMPTY;
            if (k == 3) {
                p.board[from + fwd] = p.board[from]; p.board[from] = EMPTY;
                candidate = MK(to + fwd, from + fwd, 0, 0);
            }
            if (k == 4) p.board[to] = c ? WN : BN;
            expected = k == 2;
            st_check(quiet_killer_ok(&p, candidate) == expected,
                     "pawn killer requires start rank and empty path/destination");
            for (slot = 0; slot < 2; slot++) {
                n = gen_moves(&p, st_all); na = 0;
                mgen_init(&p, &mg, 0, slot ? 0 : candidate, slot ? candidate : 0, 0);
                while ((m = next_move(&p, &mg)) != 0 && na < 256) st_got[na++] = m;
                st_check(na == n && m == 0, "killer picker move count equals generator");
                for (i = 0; i < n; i++) {
                    j = 0;
                    for (ep = 0; ep < na; ep++) if (st_got[ep] == st_all[i]) j++;
                    st_check(j == 1, "killer picker yields each generated move once");
                }
            }
        }
    }
    nnue_active = 0;
    printf("searchtest: checks=%d failures=%d\n", st_checks, st_fails);
    return st_fails != 0;
}

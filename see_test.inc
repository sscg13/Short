/* Diagnostic-only SEE oracle and integration checks. No shipping footprint. */
typedef struct { const char *fen; i16 from, to, flags, expected; } SeeCase;
static const SeeCase see_cases[] = {
    { "7k/8/8/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,1 },
    { "7k/8/5p2/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/5p2/4p3/3P4/8/8/K7 w - - 0 1", 0x33,0x44,0,1 },
    { "7k/8/5n2/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,1 },
    { "7k/8/6n1/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/8/4p2r/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/8/1r2p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/4r3/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/5b2/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/2b5/3p4/2Q5/8/8/K7 w - - 0 1", 0x32,0x43,0,0 },
    { "7k/8/4q3/4r3/3Q4/8/8/K3R3 w - - 0 1", 0x33,0x44,0,1 },
    { "7k/8/4q3/4r3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "7k/8/4q3/4r3/3Q1P2/8/8/K7 w - - 0 1", 0x33,0x44,0,1 },
    { "4k3/8/4n3/2p5/1Q6/8/8/K3R3 w - - 0 1", 0x31,0x42,0,1 },
    { "4k3/8/4r3/3p4/2Q5/8/8/K3R3 w - - 0 1", 0x32,0x43,0,1 },
    { "8/8/5k2/4p3/3Q4/8/8/K7 w - - 0 1", 0x33,0x44,0,0 },
    { "8/8/5k2/4p3/3Q1P2/8/8/K7 w - - 0 1", 0x33,0x44,0,1 },
    { "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1", 0x44,0x53,MF_EP,1 },
    { "k6r/6P1/8/8/8/8/8/K7 w - - 0 1", 0x66,0x77,MF_PROMO_Q,1 },
    { "k7/8/8/8/8/4p3/3K4/8 w - - 0 1", 0x13,0x24,0,1 },
    { "7k/8/8/8/8/8/1pQ5/2r4K w - - 0 1", 0x12,0x02,0,1 }
};

static i16 see_test_value(i16 pc) {
    static const i16 values[7] = {0,100,320,330,500,900,10000};
    return values[TY(pc)];
}

/* Independent full swap list: use real movegen/make/unmake and propagate
   material scores backwards. Skip exchanges with tied least attackers (their
   x-rays can depend on which equal-value piece is chosen) or promotions. */
static i16 see_oracle(Pos *p, u16 initial, i16 *score) {
    static Undo undo[32];
    static u16 moves[32], list[256];
    i16 gain[32], depth = 0, to = mto(initial), target, skip = 0, d;
    if (mfl(initial) || TY(p->board[mfrom(initial)]) == 6) return 0;
    gain[0] = see_test_value(p->board[to]);
    target = see_test_value(p->board[mfrom(initial)]);
    moves[0] = initial;
    do_make(p, initial, &undo[0]);
    for (;;) {
        i16 i, n = gen_caps(p, list), least = 11000, ties = 0;
        u16 selected = 0;
        for (i = 0; i < n; ++i) {
            Undo u;
            u16 m = list[i];
            i16 val, legal;
            if (mto(m) != to) continue;
            val = see_test_value(p->board[mfrom(m)]);
            do_make(p, m, &u);
            legal = !is_attacked(p, p->ks[p->side ^ 1], p->side);
            undo_move(p, m, &u);
            if (!legal) continue;
            if (ispromo(m)) { skip = 1; break; }
            if (val < least) { least = val; selected = m; ties = 1; }
            else if (val == least) ++ties;
        }
        if (skip || ties > 1) { skip = 1; break; }
        if (!selected) break;
        if (depth == 31) { skip = 1; break; }
        ++depth;
        gain[depth] = target - gain[depth - 1];
        target = least;
        moves[depth] = selected;
        do_make(p, selected, &undo[depth]);
        if (least == 10000) break;
    }
    for (d = depth; d >= 0; --d) undo_move(p, moves[d], &undo[d]);
    while (depth > 0) {
        if (gain[depth] > -gain[depth - 1]) gain[depth - 1] = -gain[depth];
        --depth;
    }
    *score = gain[0];
    return !skip;
}

static void see_flip(Pos *p) {
    i16 sq, pc;
    Pos q = *p;
    for (sq = 0; sq < 128; sq = (sq + 9) & ~8) {
        pc = q.board[sq]; p->board[0x77 - sq] = pc ? pc ^ 8 : EMPTY;
    }
    p->side ^= 1;
    p->ks[0] = 0x77 - q.ks[1]; p->ks[1] = 0x77 - q.ks[0];
    if (q.ep >= 0) p->ep = 0x77 - q.ep;
}

int see_selftest(void) {
    static Pos p, before;
    static u16 list[256], legal_list[256];
    i16 i, flip, fails = 0, oracle_cases = 0, restoration_cases = 0;
    i16 old_active = nnue_active, old_enabled = nnue_enabled;
    u16 rng = 19317;
    nnue_active = 0;
    /* A pinned rook cannot leave the e-file to recapture on d5. */
    parse_fen(&p,"4k3/8/8/3pr3/2Q5/8/8/K3R3 w - - 0 1"); before = p;
    if (!see_nonnegative(&p,MK(0x43,0x32,0,0)) || memcmp(&p,&before,sizeof p)) ++fails;
    ++restoration_cases;
    for (i = 0; i < (i16)(sizeof see_cases / sizeof see_cases[0]); ++i) {
        for (flip = 0; flip < 2; ++flip) {
            u16 m;
            i16 got;
            parse_fen(&p, see_cases[i].fen);
            if (flip) see_flip(&p);
            m = MK(flip ? 0x77-see_cases[i].to : see_cases[i].to,
                   flip ? 0x77-see_cases[i].from : see_cases[i].from,see_cases[i].flags,0);
            before = p; got = see_nonnegative(&p, m);
            if (got != see_cases[i].expected || memcmp(&p, &before, sizeof p)) {
                printf("SEE fixture %d flip=%d got=%d expected=%d\n",
                       i,flip,got,see_cases[i].expected); ++fails;
            }
            ++restoration_cases;
        }
    }
    /* Deterministic legal playouts exercise crowded boards and state restore. */
    parse_fen(&p, bench_fens[0]);
    for (i = 0; i < 2000; ++i) {
        i16 j, n = gen_moves(&p, list), nlegal = 0;
        for (j = 0; j < n; ++j) {
            Undo u;
            u16 m = list[j];
            i16 legal, cap = p.board[mto(m)], got, score;
            do_make(&p, m, &u);
            legal = !is_attacked(&p, p.ks[p.side ^ 1], p.side);
            undo_move(&p, m, &u);
            if (!legal) continue;
            legal_list[nlegal++] = m;
            if (!cap && mfl(m) != MF_EP && !ispromo(m)) continue;
            before = p; got = see_nonnegative(&p, m);
            if (memcmp(&p, &before, sizeof p)) ++fails;
            ++restoration_cases;
            if (cap && see_oracle(&p, m, &score)) {
                ++oracle_cases;
                if (got != (score >= 0)) {
                    printf("SEE oracle ply=%d move=%04X got=%d score=%d\n",i,m,got,score);
                    ++fails;
                }
            }
            if (memcmp(&p, &before, sizeof p)) ++fails;
        }
        if (!nlegal || i % 80 == 79) parse_fen(&p, bench_fens[(i/80) % BENCH_N]);
        else {
            Undo u;
            rng ^= (u16)(rng << 7); rng ^= rng >> 9; rng ^= (u16)(rng << 8);
            do_make(&p, legal_list[rng % nlegal], &u);
        }
    }
    /* A losing capture is pruned before make; check evasions bypass SEE. */
#if defined(PROFILE) || defined(VCLOCK)
    nnue_enabled = 0; stop_now = 0; deadline = 0; vtime_mode = 0; g_sigs_n = 0;
    parse_fen(&p, see_cases[1].fen); before = p;
    rep_reset(); vclock_reset(); qsearch(&p, -INF, INF, 0, 0, 1);
    if (c_qnodes != 1 || !c_see || rep_n || memcmp(&p,&before,sizeof p)) ++fails;
    parse_fen(&p,"7k/8/8/8/8/2p5/1p6/KQ6 w - - 0 1"); before = p;
    rep_reset(); vclock_reset(); qsearch(&p, -INF, INF, 0, 0, 1);
    if (c_see || c_qnodes < 2 || rep_n || memcmp(&p,&before,sizeof p)) ++fails;
#endif
    nnue_enabled = old_enabled; nnue_active = old_active;
    printf("SEE selftest: fixtures=%d oracle=%d restoration=%d failures=%d\n",
           (int)(1+2*sizeof see_cases/sizeof see_cases[0]),oracle_cases,restoration_cases,fails);
    return fails != 0;
}

int see_microbench(void) {
    static Pos p;
    i16 phase, i;
    volatile i16 sink = 0;
    nnue_active = 0;
    for (phase = -1; phase < (i16)(sizeof see_cases / sizeof see_cases[0]); ++phase) {
        clock_t begin, end;
        i16 c = phase < 0 ? 0 : phase;
        u16 m = MK(see_cases[c].to,see_cases[c].from,see_cases[c].flags,0);
        parse_fen(&p,see_cases[c].fen);
        vclock_reset();
        begin = clock();
        for (i = 0; i < 4000; ++i) {
            if (phase < 0) sink ^= 1;
            else sink ^= see_nonnegative(&p,m);
        }
        end = clock();
        printf("seemicro phase=%d calls=4000 ticks=%ld hz=%ld sink=%d",
               phase,(long)(end-begin),(long)CLOCKS_PER_SEC,sink);
#if defined(PROFILE) || defined(VCLOCK)
        printf(" see=%ld steps=%ld attacks=%ld",(long)c_see,(long)c_see_step,(long)c_isattacked);
#endif
        printf("\n");
    }
    return 0;
}

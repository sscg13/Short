/* Diagnostic-only tests: controlled real clock and both virtual models. */
static clock_t time_test_clock(void) {
    ++time_test_clock_calls;
    return time_test_fake_clock ? (clock_t)vtotal_nodes : clock();
}

static i16 tm_failures, tm_checks;
static void tm_check(i16 ok, const char *name) {
    ++tm_checks;
    if (!ok) { ++tm_failures; printf("FAIL time: %s\n", name); }
}

static void tm_prepare(Pos *p, const char *fen) {
    parse_fen(p, fen);
    memset(killers, 0, sizeof killers);
    memset(qhist, 0, sizeof qhist);
    tt_clear();
    g_sigs_n = 0;
    stop_now = 0;
    search_set_limits(0, 0);
    vclock_reset();
    time_test_clock_calls = 0;
}

int time_selftest(void) {
    i32 soft, hard, first_nodes, first_ms, second_ms;
    i16 i, model, save_nnue = nnue_enabled;
    u16 first, full, m;
    Pos p, original;
    tm_checks = tm_failures = 0;
    post_on = 0;
    time_test_fake_clock = 1;
    time_limits_ms(7200000L, 40, 0, &soft, &hard);
    tm_check(soft == 90000L && hard == 270000L, "2h/40 allocation");
    time_limits_ms(1000, 1, 0, &soft, &hard);
    tm_check(soft == 970 && hard == 970, "last move reserve");
    time_limits_ms(1000, 40, 2000, &soft, &hard);
    tm_check(soft == 485 && hard == 970, "increment cannot exceed bank");
    time_limits_ms(1000, 40, 0, &soft, &hard);
    tm_check(soft == 12 && hard == 37, "short control");
    time_limits_ms(1000, 0, -1, &soft, &hard);
    tm_check(soft == 12 && hard == 37, "sudden death horizon");
    time_limits_ms(7200000L, 40, 2147483647L, &soft, &hard);
    tm_check(soft == 3599985L && hard == 7199970L, "large increment does not wrap");
    for (i = -1; i <= 100; ++i) {
        time_limits_ms(i, 40, 0, &soft, &hard);
        tm_check(soft >= 1 && hard >= soft &&
                 hard <= (i > TIME_MARGIN_MS ? i - TIME_MARGIN_MS : 1),
                 "tiny/empty bank stays bounded");
    }
    tm_check(ms_ticks(7200000L) == (clock_t)7200 * CLOCKS_PER_SEC,
             "full control tick conversion");
    tm_check(ms_ticks(1) > 0 && ms_ticks(2500) == (clock_t)5 * CLOCKS_PER_SEC / 2,
             "fractional-second tick conversion");
    tm_check(vclock_selftest() == 0, "virtual boundaries/refills");

    vtime_mode = 0;
    /* Depth 2 crosses the 64-node real-clock poll in both eval modes. */
    tm_prepare(&p, bench_fens[1]);
    first = think(&p, 1);
    first_nodes = vtotal_nodes;
    tm_check(first != 0 && time_test_depth == 1, "baseline completed depth");
    tm_prepare(&p, bench_fens[1]);
    original = p;
    soft_deadline = 1;
    deadline = (clock_t)1000000L;
    m = think(&p, 4);
    tm_check(m == first && time_test_depth == 1 && !stop_now,
             "real soft stop after completed depth");
    tm_check(memcmp(&p, &original, sizeof p) == 0 && rep_n == 0 && !nnue_active,
             "soft stop restores position/history/NNUE");
    tm_prepare(&p, bench_fens[1]);
    original = p;
    deadline = (clock_t)(first_nodes + 1);
    m = think(&p, 4);
    tm_check(stop_now && time_test_depth == 1 && m == first,
             "real hard stop keeps prior completed depth");
    tm_check(memcmp(&p, &original, sizeof p) == 0 && rep_n == 0 && !nnue_active,
             "hard stop restores position/history/NNUE");
    /* This depth-1 search reaches the 64-node clock poll; the starting
       position can finish before the first poll when its children qsearch. */
    tm_prepare(&p, bench_fens[1]);
    deadline = 1;
    m = think(&p, 4);
    tm_check(m == 0 && time_test_depth == 0 && stop_now,
             "no completed depth returns legal-fallback sentinel");

    /* Pseudo-legal king moves can all be illegal. Root search must return a
       terminal score and an empty PV instead of retrying aspiration forever. */
    {
        static const char *terminal_fens[] = {
            "8/8/8/8/8/2k5/1q6/K7 w - - 0 1",
            "8/8/8/8/8/1qk5/8/K7 w - - 0 1"
        };
        for (i = 0; i < 2; ++i) {
            Score score;
            tm_prepare(&p, terminal_fens[i]); original = p;
            g_sigs_n = 1; g_sigs[0] = p.sig; rep_reset();
            root_n = gen_moves(&p, root_m);
            score = alphabeta(&p, 2, -INF, INF, 0, 0, NODE_PV);
            tm_check(score == (i ? 0 : -MATE) && !pv_len[0] && !rep_n &&
                     g_sigs_n == 1 && g_sigs[0] == p.sig &&
                     memcmp(&p, &original, sizeof p) == 0,
                     "terminal root score/PV/history");
            m = think(&p, 4);
            tm_check(!m && !pv_len[0] && !rep_n && !nnue_active &&
                     memcmp(&p, &original, sizeof p) == 0,
                     "terminal root driver returns no move");
        }
    }

    /* A target reached inside a failed aspiration pass must not publish a
       partial depth. Exercise all bench positions against a full depth 2. */
    for (i = 0; i < BENCH_N; ++i) {
        tm_prepare(&p, bench_fens[i]);
        think(&p, 1);
        first_nodes = vtotal_nodes;
        tm_prepare(&p, bench_fens[i]);
        full = think(&p, 2);
        tm_prepare(&p, bench_fens[i]);
        soft_deadline = (clock_t)(first_nodes + 1);
        deadline = (clock_t)10000000L;
        m = think(&p, 4);
        tm_check(time_test_depth == 2 && m == full && !stop_now,
                 "soft target preserves full depth and aspiration retries");
    }

    for (model = VCPU_80286; model <= VCPU_80186; ++model) {
        vcpu_model = model;
        vcpu_khz = 25000;
        vtime_mode = 1;
        tm_prepare(&p, bench_fens[1]);
        first = think(&p, 1);
        first_ms = vclock_charge();
        tm_prepare(&p, bench_fens[1]);
        think(&p, 2);
        second_ms = vclock_charge();
        tm_check(second_ms > first_ms + 100, "virtual depth 2 costs more than depth 1");
        tm_prepare(&p, bench_fens[1]);
        original = p;
        vclock_set_limits(1, second_ms + 10000);
        /* Deliberately stale host deadlines must be ignored. */
        soft_deadline = deadline = 1;
        m = think(&p, 4);
        tm_check(m == first && time_test_depth == 1 && !stop_now,
                 "virtual soft stops only after complete depth");
        tm_check(time_test_clock_calls == 0, "virtual soft ignores host clock");
        tm_check(memcmp(&p, &original, sizeof p) == 0 && rep_n == 0,
                 "virtual soft restores position/history");
        tm_prepare(&p, bench_fens[1]);
        original = p;
        vclock_set_limits(0, first_ms + (second_ms - first_ms) / 2);
        soft_deadline = deadline = 1;
        m = think(&p, 4);
        tm_check(stop_now && time_test_depth == 1 && m == first,
                 "virtual hard interrupts incomplete depth");
        tm_check(time_test_clock_calls == 0, "virtual hard ignores host clock");
        tm_check(memcmp(&p, &original, sizeof p) == 0 && rep_n == 0 && !nnue_active,
                 "virtual hard restores position/history/NNUE");
    }
    nnue_enabled = save_nnue;
    vtime_mode = 0;
    vcpu_model = VCPU_80286;
    vcpu_khz = 25000;
    time_test_fake_clock = 0;
    search_set_limits(0, 0);
    vclock_newgame();
    printf("time selftest: checks=%d failures=%d\n", tm_checks, tm_failures);
    return tm_failures ? 1 : 0;
}

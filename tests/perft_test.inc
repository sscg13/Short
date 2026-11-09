/* perft_test.inc - movegen/make/unmake validation against published counts.

   Included by chess.c under PERFT_TEST. Run as `chess perft ...`:

     perft [depth] [position]     suite position 0..5 (default depth 5, pos 0)
     perft [depth] 8              first line of fen.txt
     perft [depth] 9 <fen>        custom FEN
     perft [depth] <position> s   one line per root move (split counts)

   Positions 0..5 index the shared fens[] suite in chess.c. A 0 expectation
   means the published depth is not tabulated; the line still prints "ok"
   without comparing. Any mismatch prints "*** WRONG ***" and makes the
   command exit non-zero, so `make test` catches it. */

static const u32 perft_expv[6][6] = {
    { 20, 400, 8902, 197281, 4865609, 119060324 },
    { 48, 2039, 97862, 4085603, 193690690, 0 },
    { 14, 191, 2812, 43238, 674624, 11030083 },
    { 6, 264, 9467, 422333, 15833292, 706045033 },
    { 44, 1486, 62379, 2103487, 89941194, 0 },
    { 46, 2079, 89890, 3894594, 164075551, 0 }
};

static i32 perft(Pos *p, i16 depth) {
    i32 nodes = 0;
    u16 *list;
    i16 n, i;

    if (depth <= 0) return 1;          /* perft(0) = the position itself */
    list = movebuf[depth];
    n = gen_moves(p, list);

    for (i = 0; i < n; i++) {
        Undo u;
        i16 us;
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

int perft_selftest(int argc, char **argv) {
    i16 depth = 5, test = 0, i, splitsel = 0;
    i16 bad = 0;
    Pos pos;

    if (argc > 0) depth = atoi(argv[0]);
    if (argc > 1) test = atoi(argv[1]);
    if (argc > 2 && strcmp(argv[argc - 1], "s") == 0) { splitsel = 1; argc--; }
    if (depth < 1) depth = 1;
    if (depth > 6) depth = 6;

    if (test == 9 && argc > 2)
        parse_fen(&pos, argv[2]);
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
    } else if (test >= 0 && test <= 5) {
        parse_fen(&pos, fens[test]);
    } else {
        parse_fen(&pos, fens[0]);
    }

    printf("pos=%d CLOCKS_PER_SEC=%d\n", test + 1, (int)CLOCKS_PER_SEC);

    if (splitsel) {
        u16 *list = movebuf[29];
        i16 n = gen_moves(&pos, list);
        for (i = 0; i < n; i++) {
            Undo u;
            i16 us;
            i32 c;
            do_make(&pos, list[i], &u);
            us = pos.side ^ 1;
            if (!is_attacked(&pos, pos.ks[us], pos.side))
                c = perft(&pos, depth - 1);
            else
                c = -1;
            undo_move(&pos, list[i], &u);
            printf("m=%02X%02X p=%d -> %lu\n",
                   mfrom(list[i]), mto(list[i]), mpro(list[i], us ? 8 : 0),
                   (unsigned long)c);
        }
        return 0;
    }

    for (i = 1; i <= depth; i++) {
        u32 want, got;
        i32 nodes;
        clock_t t0, t1;
        double secs;

        t0 = engine_clock();
        nodes = perft(&pos, i);
        t1 = engine_clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        got = (u32)nodes;
        want = (test >= 0 && test <= 5) ? perft_expv[test][i - 1] : 0;
        if (want && got != want) bad = 1;
        printf("perft(%d)=%lu  %8.2fs  %6lu nps  %s\n",
               i, (unsigned long)got, secs,
               secs > 0 ? (unsigned long)((double)nodes / secs) : 0UL,
               (want && got != want) ? "*** WRONG ***" : "ok");
    }
    return bad;
}

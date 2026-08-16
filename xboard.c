/* xboard.c - CECP/xboard protocol, game state, move history */

#include "engine.h"

static Pos gpos;
static i16 force_mode = 1, game_over = 0;
i16 post_on = 0;
i16 g_half, g_full;              /* halfmove clock, fullmove number */
/* game-history position signatures. 128 x 8 bytes = 1 KB fits DGROUP (was 1024
   x 8 = 8 KB far); cleared on every zeroing move (see apply_move). No takeback
   (remove/undo) support, so this is the only move-history structure. */
Sig g_sigs[MAX_G_SIGS];
i16 g_sigs_n;
static i16 xb_st = 0, xb_time_cs = 0;
static i16 xb_level_mps = 0, xb_level_inc = 0;   /* "level mps base inc" control */

static FILE *fdbg;                      /* protocol debug log (chess_debug.txt) */

void dbgf(const char *fmt, ...) {
    va_list ap;
    if (!fdbg) return;
    va_start(ap, fmt);
    vfprintf(fdbg, fmt, ap);
    va_end(ap);
    fflush(fdbg);
}

void xb_outf(const char *fmt, ...) {
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

static const char *start_fen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static i16 legal_move(Pos *p, u16 m) {
    Undo u;
    i16 us, ok;
    do_make(p, m, &u);
    us = p->side ^ 1;
    ok = !is_attacked(p, p->ks[us], p->side);
    undo_move(p, m, &u);
    return ok;
}

static void move_to_coord(u16 m, char *buf) {
    static const char pn[] = " PNBRQK";
    i16 f = mfrom(m), t = mto(m);
    buf[0] = 'a' + (f & 7);
    buf[1] = '1' + (f >> 4);
    buf[2] = 'a' + (t & 7);
    buf[3] = '1' + (t >> 4);
    if (ispromo(m)) { buf[4] = pn[mfl(m) + 1]; buf[5] = 0; }
    else buf[4] = 0;
}

static u16 parse_coord(Pos *p, const char *s) {
    u16 *list = movebuf[30];
    i16 n = gen_moves(p, list);
    i16 i, pr = 0;
    if (s[0] < 'a' || s[0] > 'h' || s[2] < 'a' || s[2] > 'h') return 0;
    {
        i16 f = (s[1] - '1') * 16 + (s[0] - 'a');
        i16 t = (s[3] - '1') * 16 + (s[2] - 'a');
        switch (s[4]) {
            case 'n': case 'N': pr = WN; break;
            case 'b': case 'B': pr = WB; break;
            case 'r': case 'R': pr = WR; break;
            case 'q': case 'Q': pr = WQ; break;
        }
        for (i = 0; i < n; i++) {
            if (mfrom(list[i]) != f) continue;
            if (mto(list[i]) != t) continue;
            if (!legal_move(p, list[i])) continue;
            if (pr) { if (ispromo(list[i]) && mfl(list[i]) + 1 == TY(pr)) return list[i]; }
            else if (!ispromo(list[i])) return list[i];
        }
    }
    return 0;
}

static u16 parse_castle(Pos *p, const char *s) {
    u16 *list = movebuf[30];
    i16 n = gen_moves(p, list), i, want = 0;
    if ((s[0] == 'O' || s[0] == '0') && s[1] == '-' && (s[2] == 'O' || s[2] == '0')) {
        if (s[3] == '-' && (s[4] == 'O' || s[4] == '0')) want = 0x02;
        else want = 0x06;
    } else return 0;
    for (i = 0; i < n; i++)
        if (mfl(list[i]) == MF_CASTLE && (mto(list[i]) & 0x0F) == want)
            return list[i];
    return 0;
}

static i16 draw_claim(Pos *p) {
    i16 i, occ = 0, minors = 0;
    Sig s = p->sig;
    if (g_half >= MAX_HALF) { dbgf("draw_claim: halfmove g_half=%d\n", g_half); return 1; }
    {
        i16 c = 0;
        for (i = 0; i < g_sigs_n; i++)
            if (g_sigs[i] == s) {
                if (++c >= 3) {
                    /* %llX is not portable to the 16-bit libc; print hi/lo */
                    dbgf("draw_claim: repetition sig=%08lX%08lX count=%d g_sigs_n=%d\n",
                         (unsigned long)(s >> 32), (unsigned long)s, c, g_sigs_n);
                    return 1;
                }
            }
    }
    for (i = 0; i < 128; i++) {
        i16 pc = p->board[i];
        i16 ty;
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

static void apply_move(Pos *p, u16 m) {
    Undo u;
    i16 pc = p->board[mfrom(m)];
    do_make(p, m, &u);
    if (TY(pc) == 1 || u.cap) {
        /* zeroing move (pawn move or capture): irreversible, so no game-history
           position before it can repeat after it - drop the history. */
        g_half = 0; g_sigs_n = 0;
    } else {
        g_half++;
    }
    if (CO(pc) == 8) g_full++;
    if (g_sigs_n < MAX_G_SIGS) g_sigs[g_sigs_n++] = pos_sig(p);
}

static void xb_go(void) {
    u16 *list = movebuf[28];
    i16 n = gen_moves(&gpos, list);
    i16 i, legal = 0;
    u16 m = 0, first = 0;
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
    if (vtime_mode) {
        /* virtual clock: ignore the GUI entirely; the budget is the 40/2h+
           20/1h control at CPU_model/CPU_KHz speed (see vclock.c) */
        i32 bms = vclock_budget_ms();
        vclock_reset();
        vclock_set_budget(bms);
        deadline = 0;
        dbgf("go(virtual): model=%d khz=%ld budget_ms=%ld\n",
             vcpu_model, (long)vcpu_khz, (long)bms);
    } else {
        i32 budget = 3000;
        i32 remaining_ms = (i32)xb_time_cs * 10;
        if (xb_st > 0) {
            budget = (i32)xb_st * 1000;         /* fixed seconds per move */
        } else if (xb_level_mps > 0) {
            /* tournament control "level mps base inc": remaining/mps + increment */
            if (remaining_ms > 0)
                budget = remaining_ms / xb_level_mps + (i32)xb_level_inc * 1000;
        } else if (xb_time_cs > 0) {
            i32 cs = xb_time_cs / 40;           /* ~1/40 of remaining clock */
            if (cs < 50) cs = 50;
            budget = cs * 10;                    /* centiseconds -> ms */
        }
        /* never allocate more than the time actually left (minus the margin) */
        if (remaining_ms > TIME_MARGIN_MS + 100 && budget > remaining_ms - TIME_MARGIN_MS)
            budget = remaining_ms - TIME_MARGIN_MS;
        if (budget < 100) budget = 100;
        /* deadline is in clock() ticks; budget/TIME_MARGIN_MS are ms, so scale
           by CLOCKS_PER_SEC/1000 (a no-op where CLOCKS_PER_SEC == 1000, exact
           on Linux gcc where it is 1,000,000 - otherwise a 3 s move would
           abort after ~3 ms there). */
        deadline = (i32)(clock() + (long)(budget - TIME_MARGIN_MS) * CLOCKS_PER_SEC / 1000);
        dbgf("go: side=%d st=%d time_cs=%ld level=%d+%d budget=%ld deadline=%ld\n",
             gpos.side, xb_st, (long)xb_time_cs, xb_level_mps, xb_level_inc,
             (long)budget, (long)deadline);
    }
    m = think(&gpos, 10);
    if (vtime_mode) {
        i32 cn = vtotal_nodes;
        i32 cms = vclock_charge();
        dbgf("vclock nodes=%ld consumed=%ldms\n", (long)cn, (long)cms);
    }
    deadline = 0;
    stop_now = 0;
    if (m == 0) m = first;
    move_to_coord(m, b);
    xb_outf("move %s", b);
    apply_move(&gpos, m);
}

static void xb_reset(void) {
    parse_fen(&gpos, start_fen);
    g_sigs_n = 0;
    g_sigs[g_sigs_n++] = pos_sig(&gpos);
    tt_clear();                              /* fresh game: empty the TT */
    force_mode = 1; game_over = 0;
    stop_now = 0; deadline = 0;          /* keep xb_time_cs/xb_st/post_on: WinBoard
                                            re-sends them at each new game anyway */
    vclock_newgame();
}

static char *skipsp(char *s) { while (*s == ' ') s++; return s; }

/* CECP `option name=value` / UCI-ish `setoption name X value Y` handler. */
static void xb_option(const char *nm, const char *val) {
    char lo[40];
    i16 i;
    for (i = 0; nm[i] && i < 39; i++) {
        char c = nm[i];
        lo[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    lo[i] = 0;
    if (strcmp(lo, "threads") == 0) {
        /* single-threaded by design; accepted for OpenBench, ignored */
    } else if (strcmp(lo, "hash") == 0) {
        /* no transposition table yet; accepted and ignored */
    } else if (strcmp(lo, "cpu_model") == 0) {
        vclock_set_model(val);
    } else if (strcmp(lo, "cpu_khz") == 0) {
        vclock_set_khz(atol(val));
    } else if (strcmp(lo, "virtualtime") == 0) {
        vclock_set_enabled(val);
    }
    /* unknown options are silently accepted (CECP requires tolerating them) */
    dbgf("option '%s'='%s'\n", lo, val ? val : "");
}

int xboard_main(void) {
    char line[256];
    char fname[64];
    /* per-process log file so two self-play engines don't clobber each other */
    sprintf(fname, "chess_debug_%ld.txt", DBG_PID());
    fdbg = fopen(fname, "w");
    if (fdbg) { fprintf(fdbg, "== chess protocol debug ==\n"); fflush(fdbg); }
    xb_reset();
    while (fgets(line, sizeof(line), stdin)) {
        char *p = line;
        i16 i;
        for (i = 0; line[i] && line[i] != '\n' && line[i] != '\r'; i++);
        line[i] = 0;
        while (*p == ' ') p++;
        dbgf(">> %s\n", p);
        if (strncmp(p, "xboard", 6) == 0) {
        } else if (strncmp(p, "protover", 8) == 0) {
            /* announce the options FIRST, done=1 on the last feature line.
               CECP combo options use `-combo` with `///` separators and the
               default marked by a leading `*`; -check takes 1/0. */
            xb_outf("feature option=\"Threads -spin 1 1 16\"");
            xb_outf("feature option=\"Hash -spin 32 32 1024\"");
            xb_outf("feature option=\"CPU_model -combo *80286 /// 8088 /// 8086\"");
            xb_outf("feature option=\"CPU_KHz -spin 25000 1000 50000\"");
            xb_outf("feature option=\"VirtualTime -check 0\"");
            xb_outf("feature myname=\"Short\" setboard=1 usermove=1 ping=1 playother=1 done=1");
        } else if (strncmp(p, "new", 3) == 0) {
            xb_reset();
        } else if (strncmp(p, "setboard", 8) == 0) {
            /* like `new` but with a given FEN: full state reset so a setboard
               followed directly by `go` works even after a finished game. */
            parse_fen(&gpos, skipsp(p + 8));
            g_sigs_n = 0;
            g_sigs[g_sigs_n++] = pos_sig(&gpos);
            tt_clear();                              /* fresh position: empty the TT */
            force_mode = 1; game_over = 0;
            stop_now = 0; deadline = 0;
            vclock_newgame();
        } else if (strncmp(p, "force", 5) == 0) {
            force_mode = 1;
        } else if (strncmp(p, "playother", 9) == 0) {
            xb_go();
        } else if (strncmp(p, "go", 2) == 0) {
            xb_go();
        } else if (strncmp(p, "usermove", 8) == 0 || strncmp(p, "move ", 5) == 0) {
            char *mv = skipsp(p + (strncmp(p, "usermove", 8) == 0 ? 8 : 5));
            u16 m = parse_coord(&gpos, mv);
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
            break;
        } else if (strncmp(p, "result", 6) == 0) {
            game_over = 1;
        } else if (strncmp(p, "draw", 4) == 0) {
            xb_outf("1/2-1/2 {Engine accepts draw offer}");
        } else if (strncmp(p, "post", 4) == 0) {
            post_on = 1;
        } else if (strncmp(p, "nopost", 6) == 0) {
            post_on = 0;
        } else if (strncmp(p, "option", 6) == 0) {
            char *eq = strchr(p, '=');
            if (eq) {
                char *nm = skipsp(p + 6);
                int nlen = (int)(eq - nm);
                char name[40];
                while (nlen > 0 && nm[nlen - 1] == ' ') nlen--;
                if (nlen > 39) nlen = 39;
                memcpy(name, nm, nlen);
                name[nlen] = 0;
                xb_option(name, skipsp(eq + 1));
            }
        } else if (strncmp(p, "setoption", 9) == 0) {
            /* lenient UCI-style fallback: setoption name Threads value 1 */
            char *rest = skipsp(p + 9);
            char *val;
            if (strncmp(rest, "name", 4) == 0) rest = skipsp(rest + 4);
            val = strstr(rest, " value ");
            if (!val) val = strstr(rest, " value");
            if (val) {
                char name[40];
                int nlen = (int)(val - rest);
                while (nlen > 0 && rest[nlen - 1] == ' ') nlen--;
                if (nlen > 39) nlen = 39;
                memcpy(name, rest, nlen);
                name[nlen] = 0;
                xb_option(name, skipsp(val + 6));
            }
        } else if (p[0] == '?') {
            stop_now = 1;
        } else {
            /* remove/undo/hard/easy/random/name/accepted/rejected/variant/analyze/
               exit/bk/edit/hint: ignored (no takeback support - see MEMORY.md) */
        }
    }
    dbgf("stdin closed, exiting\n");
    if (fdbg) fclose(fdbg);
    fdbg = NULL;
    return 0;
}

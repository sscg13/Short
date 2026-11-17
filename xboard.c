/* xboard.c - CECP/xboard protocol, game state, move history */

#include "engine.h"

static Pos gpos;
static i16 force_mode = 1, game_over = 0;
i16 post_on = 0;
i16 uci_mode = 0;                /* set by the `uci` handshake; see engine.h */
i16 g_half, g_full;              /* halfmove clock, fullmove number */
/* game-history position signatures. 128 x 8 bytes = 1 KB fits DGROUP (was 1024
   x 8 = 8 KB far); cleared on every zeroing move (see apply_move). No takeback
   (remove/undo) support, so this is the only move-history structure. */
Sig g_sigs[MAX_G_SIGS];
i16 g_sigs_n;
static i16 xb_st = 0;
static i32 xb_time_cs = -1; /* full 2-hour clock fits; -1 = GUI has not sent time */
static i16 xb_level_mps = 0, xb_level_inc = 0;   /* "level mps base inc" control */

#ifdef DBG_LOG
static FILE *fdbg;                      /* protocol debug log (chess_debug.txt) */

void dbgf(const char *fmt, ...) {
    va_list ap;
    if (!fdbg) return;
    va_start(ap, fmt);
    vfprintf(fdbg, fmt, ap);
    va_end(ap);
    fflush(fdbg);
}
#endif

void xb_outf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
#ifdef DBG_LOG
    if (fdbg) {
        va_start(ap, fmt);
        vfprintf(fdbg, fmt, ap);
        va_end(ap);
        fprintf(fdbg, "\n");
        fflush(fdbg);
    }
#endif
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
    {
        i32 soft_ms, hard_ms;
        if (vtime_mode) {
            vclock_limits_ms(&soft_ms, &hard_ms);
            vclock_reset();
            vclock_set_limits(soft_ms, hard_ms);
            search_set_limits(0, 0);
            dbgf("go(virtual): model=%d khz=%ld soft_ms=%ld hard_ms=%ld\n",
                 vcpu_model, (long)vcpu_khz, (long)soft_ms, (long)hard_ms);
        } else {
            i32 remaining_ms = xb_time_cs >= 0 ? xb_time_cs * 10 : 0;
            if (xb_st > 0) {
                /* Fixed move time is a cap, not a bank to borrow from. */
                i32 available = (i32)xb_st * 1000;
                if (xb_time_cs >= 0 && remaining_ms < available) available = remaining_ms;
                hard_ms = available > TIME_MARGIN_MS ? available - TIME_MARGIN_MS : 1;
                soft_ms = hard_ms;
            } else if (xb_time_cs >= 0) {
                i16 moves_left = xb_level_mps > 0
                    ? xb_level_mps - ((g_full > 0 ? g_full - 1 : 0) % xb_level_mps) : 40;
                time_limits_ms(remaining_ms, moves_left, (i32)xb_level_inc * 1000,
                               &soft_ms, &hard_ms);
            } else soft_ms = hard_ms = 3000 - TIME_MARGIN_MS;
            search_set_limits(soft_ms, hard_ms);
            dbgf("go: side=%d st=%d time_cs=%ld level=%d+%d soft_ms=%ld hard_ms=%ld\n",
                 gpos.side, xb_st, (long)xb_time_cs, xb_level_mps, xb_level_inc,
                 (long)soft_ms, (long)hard_ms);
        }
    }
    m = think(&gpos, 10);
    if (vtime_mode) {
        i32 cn = vtotal_nodes;
        i32 cms = vclock_charge();
#ifndef DBG_LOG
        (void)cn;
        (void)cms;                      /* charge has the side effect; values are log-only */
#else
        dbgf("vclock nodes=%ld consumed=%ldms\n", (long)cn, (long)cms);
#endif
    }
    search_set_limits(0, 0);
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
    search_clear_ordering();
    tt_clear();                              /* fresh game: empty the TT */
    force_mode = 1; game_over = 0;
    stop_now = 0; search_set_limits(0, 0);
    /* Keep time/st/post: WinBoard re-sends them at each new game. */
    vclock_newgame();
}

static char *skipsp(char *s) { while (*s == ' ') s++; return s; }

#if PROTO_UCI
/* Space-separated token splitter that mutates the line: returns the next
   token and advances *s past it. UCI commands are the only users (`position
   ... moves ...`, `go wtime ...`); CECP parses fixed prefixes instead, so the
   two modes never see a half-consumed buffer. */
static char *next_tok(char **s) {
    char *t;
    while (**s == ' ') (*s)++;
    if (!**s) return NULL;
    t = *s;
    while (**s && **s != ' ') (*s)++;
    if (**s) { **s = 0; (*s)++; }
    return t;
}

static u16 first_legal_move(Pos *p) {
    u16 *list = movebuf[28];
    i16 n = gen_moves(p, list), i;
    for (i = 0; i < n; i++)
        if (legal_move(p, list[i])) return list[i];
    return 0;
}

/* `uci` handshake: the same options the CECP feature block advertises, in the
   key/value form fastchess expects (OpenBench sends them as option.<Name>=<v>). */
static void uci_hello(void) {
    xb_outf("id name Short");
    xb_outf("id author sscg13");
    xb_outf("option name Threads type spin default 1 min 1 max 16");
    xb_outf("option name Hash type spin default 32 min 32 max 1024");
#ifdef VCLOCK
    xb_outf("option name CPU_model type combo default 80286"
            " var 80286 var 80186 var 8088 var 8086");
    xb_outf("option name CPU_KHz type spin default 25000 min 1000 max 50000");
    xb_outf("option name VirtualTime type check default false");
#endif
#ifdef TUNE
    search_tune_uci_features();
#endif
    xb_outf("uciok");
}

/* `position [startpos | fen <6 fields>] [moves <m1> <m2> ...]`
   Rebuilds the game state, including the repetition signatures, without
   touching the TT or the ordering tables: a match runner sends `position`
   before every `go`, so clearing them per move would throw away the reuse the
   opponent's moves already paid for. `ucinewgame` is what empties them. */
static void uci_position(char *a) {
    char fen[128];
    char *s = a, *t = next_tok(&s);
    u16 m;
    if (!t) return;
    if (strcmp(t, "startpos") == 0) {
        parse_fen(&gpos, start_fen);
    } else if (strcmp(t, "fen") == 0) {
        i16 n = 0;
        fen[0] = 0;
        while (n < 6) {
            char *f = next_tok(&s);
            if (!f) break;
            /* 128 B holds the longest legal FEN (~95); never append past it. */
            if ((i16)(strlen(fen) + strlen(f) + 2) >= (i16)sizeof(fen)) break;
            if (n) strcat(fen, " ");
            strcat(fen, f);
            n++;
        }
        if (n != 6) return;              /* malformed: keep the old position */
        parse_fen(&gpos, fen);
    } else return;
    g_sigs_n = 0;
    g_sigs[g_sigs_n++] = pos_sig(&gpos);
    game_over = 0;
    force_mode = 0;
    stop_now = 0;
    search_set_limits(0, 0);
    search_set_node_limit(0);
    if ((t = next_tok(&s)) != NULL && strcmp(t, "moves") == 0) {
        while ((t = next_tok(&s)) != NULL) {
            m = parse_coord(&gpos, t);
            if (!m) { dbgf("uci position: bad move '%s'\n", t); break; }
            apply_move(&gpos, m);
        }
    }
    dbgf("uci position: side=%d half=%d full=%d sigs=%d\n",
         gpos.side, g_half, g_full, g_sigs_n);
}

/* `go [wtime N btime N winc N binc N movestogo N | movetime N | depth N |
   nodes N | infinite]`. The limit setup mirrors xb_go, so a virtual-time game
   is decided exactly as it was under CECP; real-time limits are only used by
   the non-virtual host build and the DOS build. */
static void uci_go(char *a) {
    char *s = a, *t, *v;
    i32 wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    i32 nodes = 0;
    i16 mtg = 0, depth = 10, depth_seen = 0;
    i32 soft_ms = 0, hard_ms = 0;
    u16 m;
    char b[8];

    while ((t = next_tok(&s)) != NULL) {
        if (strcmp(t, "wtime") == 0)          { if ((v = next_tok(&s))) wtime = atol(v); }
        else if (strcmp(t, "btime") == 0)     { if ((v = next_tok(&s))) btime = atol(v); }
        else if (strcmp(t, "winc") == 0)      { if ((v = next_tok(&s))) winc = atol(v); }
        else if (strcmp(t, "binc") == 0)      { if ((v = next_tok(&s))) binc = atol(v); }
        else if (strcmp(t, "movestogo") == 0) { if ((v = next_tok(&s))) mtg = (i16)atol(v); }
        else if (strcmp(t, "movetime") == 0)  { if ((v = next_tok(&s))) movetime = atol(v); }
        else if (strcmp(t, "nodes") == 0)     { if ((v = next_tok(&s))) nodes = atol(v); }
        else if (strcmp(t, "depth") == 0)     { if ((v = next_tok(&s))) { depth = (i16)atol(v); depth_seen = 1; } }
        /* `infinite`, `ponder`, `searchmoves ...`: no special case. A bare
           `go` searches the depth cap under whatever limits are set, and a
           blocking search cannot honour a mid-search `stop` anyway. */
    }
    if (depth_seen) {
        if (depth < 1) depth = 1;
        if (depth > 10) depth = 10;      /* same cap as the CECP entry point */
    }

    if (vtime_mode) {
        vclock_limits_ms(&soft_ms, &hard_ms);
        vclock_reset();
        vclock_set_limits(soft_ms, hard_ms);
        search_set_limits(0, 0);
    } else if (movetime > 0) {
        /* A fixed move time is a cap, not a bank to borrow from. */
        i32 remaining = gpos.side ? btime : wtime;
        i32 avail = movetime;
        if (remaining >= 0 && remaining < avail) avail = remaining;
        hard_ms = avail > TIME_MARGIN_MS ? avail - TIME_MARGIN_MS : 1;
        soft_ms = hard_ms;
        search_set_limits(soft_ms, hard_ms);
    } else if ((gpos.side ? btime : wtime) >= 0) {
        i32 remaining = gpos.side ? btime : wtime;
        i32 inc = gpos.side ? binc : winc;
        i16 ml = mtg > 0 ? mtg : 40;     /* no movestogo: OpenBench's 40/2h */
        time_limits_ms(remaining, ml, inc, &soft_ms, &hard_ms);
        search_set_limits(soft_ms, hard_ms);
    } else {
        search_set_limits(0, 0);
    }
    if (nodes > 0) search_set_node_limit(nodes);
    dbgf("uci go: side=%d depth=%d nodes=%ld mtg=%d soft_ms=%ld hard_ms=%ld\n",
         gpos.side, depth, (long)nodes, mtg, (long)soft_ms, (long)hard_ms);

    m = think(&gpos, depth);
    if (vtime_mode) vclock_charge();
    search_set_limits(0, 0);
    search_set_node_limit(0);
    stop_now = 0;
    if (!m) m = first_legal_move(&gpos);      /* depth 0 completed: still answer */
    if (m) {
        move_to_coord(m, b);
        b[4] = (char)((b[4] >= 'A' && b[4] <= 'Z') ? b[4] + 32 : b[4]);
        xb_outf("bestmove %s", b);
    } else {
        xb_outf("bestmove 0000");             /* checkmate/stalemate: no move */
    }
}
#endif /* PROTO_UCI */

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
        /* fixed 64 KB transposition table; compatibility option, ignored */
#ifdef VCLOCK
    } else if (strcmp(lo, "cpu_model") == 0) {
        vclock_set_model(val);
    } else if (strcmp(lo, "cpu_khz") == 0) {
        vclock_set_khz(atol(val));
    } else if (strcmp(lo, "virtualtime") == 0) {
        vclock_set_enabled(val);
#endif
#ifdef TUNE
    } else if (search_tune_option(lo, val) < 0) {
        xb_outf("Error (invalid option value): %s", nm);
#endif
    }
    /* unknown options are silently accepted (CECP requires tolerating them) */
    dbgf("option '%s'='%s'\n", lo, val ? val : "");
}

/* `setoption name <id> value <v>`. UCI's only option syntax; CECP's
   `option name=value` is parsed in the loop and both reach xb_option(). */
static void proto_setoption(char *p) {
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
}

/* A `position ... moves ...` line carries the whole game - fastchess re-sends
   it before every move - and grows by five characters per ply. 2048 was too
   small: a 393-ply game in test 2206 hit exactly the limit, the command was
   rejected as "too long" and the engine then answered from the previous
   position (fastchess: "Black makes an illegal move"). 8192 covers ~1600 plies,
   well past any game a match runner produces; the DOS build only speaks CECP,
   where commands are short, and keeps its small buffer on the stack so DGROUP
   does not grow for a front end it never uses. */
#if PROTO_UCI
#define PROTO_LINE_MAX 8192
#else
#define PROTO_LINE_MAX 256
#endif

int xboard_main(void) {
#if PROTO_UCI
    static char line[PROTO_LINE_MAX];   /* DGROUP: no 2 KB stack frame */
#else
    char line[PROTO_LINE_MAX];
#endif
#ifdef DBG_LOG
    char fname[64];
    /* per-process log file so two self-play engines don't clobber each other */
    sprintf(fname, "chess_debug_%ld.txt", DBG_PID());
    fdbg = fopen(fname, "w");
    if (fdbg) { fprintf(fdbg, "== chess protocol debug ==\n"); fflush(fdbg); }
#endif
    xb_reset();
    while (fgets(line, sizeof(line), stdin)) {
        char *p = line;
        i16 i;
        /* Never apply a truncated option value: a long number with leading
           zeros could otherwise become a valid short integer. Drain the rest
           of this command so its tail cannot be treated as a new command. */
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n') {
            int c;
            do { c = fgetc(stdin); } while (c != '\n' && c != EOF);
            xb_outf("Error (command too long): input line");
            continue;
        }
        for (i = 0; line[i] && line[i] != '\n' && line[i] != '\r'; i++);
        line[i] = 0;
        /* UCI compares whole commands (`uci`, `isready`); trim trailing blanks
           so a runner that pads a line is not mistaken for an unknown command. */
        while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) line[--i] = 0;
        while (*p == ' ') p++;
        dbgf(">> %s\n", p);
        /* `uci` is the first command a UCI match runner sends; WinBoard and
           the DOS harnesses send `xboard`/`protover` first instead. */
#if PROTO_UCI
        if (!uci_mode && strcmp(p, "uci") == 0) {
            uci_mode = 1;
            uci_hello();
            continue;
        }
        if (uci_mode) {
            if (strcmp(p, "isready") == 0) {
                xb_outf("readyok");
            } else if (strcmp(p, "ucinewgame") == 0) {
                xb_reset();
            } else if (strncmp(p, "position", 8) == 0) {
                uci_position(skipsp(p + 8));
            } else if (strncmp(p, "go", 2) == 0) {
                uci_go(skipsp(p + 2));
            } else if (strcmp(p, "stop") == 0) {
                stop_now = 1;
            } else if (strncmp(p, "setoption", 9) == 0) {
                proto_setoption(p);
            } else if (strcmp(p, "quit") == 0) {
                break;
            }
            /* debug/register/ponderhit and blank lines: nothing to do */
            continue;
        }
#endif
        if (strncmp(p, "xboard", 6) == 0) {
        } else if (strncmp(p, "protover", 8) == 0) {
            /* announce the options FIRST, done=1 on the last feature line.
               CECP combo options use `-combo` with `///` separators and the
               default marked by a leading `*`; -check takes 1/0. */
            xb_outf("feature option=\"Threads -spin 1 1 16\"");
            xb_outf("feature option=\"Hash -spin 32 32 1024\"");
#ifdef VCLOCK
            xb_outf("feature option=\"CPU_model -combo *80286 /// 80186 /// 8088 /// 8086\"");
            xb_outf("feature option=\"CPU_KHz -spin 25000 1000 50000\"");
            xb_outf("feature option=\"VirtualTime -check 0\"");
#endif
#ifdef TUNE
            search_tune_features();
#endif
            xb_outf("feature myname=\"Short\" setboard=1 usermove=1 ping=1 playother=1 done=1");
        } else if (strncmp(p, "new", 3) == 0) {
            xb_reset();
        } else if (strncmp(p, "setboard", 8) == 0) {
            /* like `new` but with a given FEN: full state reset so a setboard
               followed directly by `go` works even after a finished game. */
            parse_fen(&gpos, skipsp(p + 8));
            g_sigs_n = 0;
            g_sigs[g_sigs_n++] = pos_sig(&gpos);
            search_clear_ordering();
            tt_clear();                              /* fresh position: empty the TT */
            force_mode = 1; game_over = 0;
            stop_now = 0; search_set_limits(0, 0);
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
            xb_time_cs = atol(skipsp(p + 4));
            if (xb_time_cs < 0) xb_time_cs = 0;
            if (xb_time_cs > 8640000L) xb_time_cs = 8640000L; /* at most 24 h */
        } else if (strncmp(p, "otim", 4) == 0) {
        } else if (strncmp(p, "st", 2) == 0) {
            xb_st = atoi(skipsp(p + 2));
        } else if (strncmp(p, "level", 5) == 0) {
            int mps = 0, base = 0, inc = 0;
            sscanf(skipsp(p + 5), "%d %d %d", &mps, &base, &inc);
            xb_st = 0;                      /* level replaces fixed move time */
            xb_level_mps = mps > 0 ? mps : 0;
            xb_level_inc = inc > 0 ? inc : 0;
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
            proto_setoption(p);        /* lenient: a UCI-style line under CECP */
        } else if (p[0] == '?') {
            stop_now = 1;
        } else {
            /* remove/undo/hard/easy/random/name/accepted/rejected/variant/analyze/
               exit/bk/edit/hint: ignored (no takeback support - see MEMORY.md) */
        }
    }
    dbgf("stdin closed, exiting\n");
#ifdef DBG_LOG
    if (fdbg) fclose(fdbg);
    fdbg = NULL;
#endif
    return 0;
}

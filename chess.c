/* chess.c - board, move generation, make/unmake, eval, perft, FEN, main */

#include "engine.h"

static const i16 kn[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const i16 ki[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 rb[4] = { -16, 1, 16, -1 };
static const i16 bb[4] = { -17, -15, 15, 17 };
static const i16 qd[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 pw[4] = { WN, WB, WR, WQ };
static const i16 pb[4] = { BN, BB, BR, BQ };
static const i16 mval[8] = { 0, 100, 320, 330, 500, 900, 0 };

u16 movebuf[32][256];

/* ---- incremental Zobrist (position signature) ----
   Piece-square keys are indexed by [piece][compact square]; compact squares
   (0..63, sq2c) halve the table vs 0x88 (64 on-board squares only). On the
   16-bit build the 7.5 KB table goes in a far segment (DGROUP has ~5 KB free;
   the game history g_sigs is now 128 x 8 = 1 KB and fits near). Keys are
   generated deterministically (fixed-seed xorshift, same sequence on gcc and
   16-bit) so both builds compute identical signatures - determinism by
    construction. */
#if defined(__WATCOMC__) && !defined(__386__)
static u64 _far zpsq[15][64];
static u64 _far zside[2];
static u64 _far zcastle[16];
static u64 _far zep[65];
#else
static u64 zpsq[15][64];
static u64 zside[2];
static u64 zcastle[16];
static u64 zep[65];
#endif

static u64 zseed = 0x9E3779B97F4A7C15ULL;

/* 64-bit xorshift (Marsaglia, period 2^64-1). The state is unsigned long long
   = 64 bits on EVERY build (16-bit Watcom, Windows and Linux gcc), so the
   Zobrist keys are byte-identical across platforms. A prior 32-bit `unsigned
   long` seed was 32-bit on Windows/16-bit but 64-bit on Linux (LP64), which
   broke bench determinism there; it ALSO confined the "64-bit" keys to a
   32-bit subspace (GF(2) rank 32), so different positions collided and fed
   bogus moves into the transposition table. */
static u64 zkey(void) {
    zseed ^= zseed << 13;
    zseed ^= zseed >> 7;
    zseed ^= zseed << 17;
    return zseed;
}

/* one-time table build (dedicated init, run at main start) */
void zob_init(void) {
    i16 pc, sq, i;
    for (pc = 1; pc < 15; pc++)
        for (sq = 0; sq < 64; sq++)
            zpsq[pc][sq] = zkey();
    zside[0] = zkey(); zside[1] = zkey();
    for (i = 0; i < 16; i++) zcastle[i] = zkey();
    for (i = 0; i < 65; i++) zep[i] = zkey();
}

/* ep-square key index: -1 (no ep) -> 64, else compact square */
#define ZEPI(ep) ((ep) < 0 ? 64 : (i16)sq2c(ep))

/* XOR the move's signature delta into p->sig. Called from BOTH do_make (after
   the board/castle/ep updates, before the side flip) and undo_move (after the
   side flip back, before the restores), when board[to] still holds the moved
   piece, board[from] is empty, p->castle/ep hold the NEW values and u the OLD.
   XOR is its own inverse, so make and undo apply the same keys and round-trip. */
static void zob_apply(Pos *p, u16 m, const Undo *u) {
    i16 from = mfrom(m), to = mto(m), fl = mfl(m);
    i16 side = p->side;                       /* old side to move (pre-flip) */
    i16 post = p->board[to];                  /* piece at dest (mover or promo) */
    i16 orig = ispromo(m) ? (side ? BP : WP) : post;
    i16 rf, rt;

    p->sig ^= zpsq[orig][sq2c(from)];
    p->sig ^= zpsq[post][sq2c(to)];
    if (fl == MF_EP) {
        i16 esq = side ? to + 16 : to - 16;   /* captured pawn square */
        p->sig ^= zpsq[side ? WP : BP][sq2c(esq)];
    } else if (u->cap) {
        p->sig ^= zpsq[u->cap][sq2c(to)];
    }
    if (fl == MF_CASTLE) {
        if (to == 0x06)      { rf = 0x07; rt = 0x05; }
        else if (to == 0x02) { rf = 0x00; rt = 0x03; }
        else if (to == 0x76) { rf = 0x77; rt = 0x75; }
        else                 { rf = 0x70; rt = 0x73; }
        p->sig ^= zpsq[side ? BR : WR][sq2c(rf)];
        p->sig ^= zpsq[side ? BR : WR][sq2c(rt)];
    }
    p->sig ^= zcastle[u->castle];
    p->sig ^= zcastle[p->castle];
    p->sig ^= zep[ZEPI(u->ep)];
    p->sig ^= zep[ZEPI(p->ep)];
    p->sig ^= zside[side];
    p->sig ^= zside[side ^ 1];
}

/* compute the signature from scratch (used at parse_fen) */
static void zob_compute(Pos *p) {
    u64 h = 0;
    i16 i;
    for (i = 0; i < 128; i++)
        if (p->board[i]) h ^= zpsq[p->board[i]][sq2c(i)];
    h ^= zside[p->side];
    h ^= zcastle[p->castle & 0xF];
    h ^= zep[ZEPI(p->ep)];
    p->sig = h;
}

#if defined(PROFILE) || defined(VCLOCK)
i32 c_make = 0;                 /* do_make entries */
i32 c_undo = 0;                 /* undo_move entries */
i32 c_gen_moves = 0;            /* gen_moves entries */
i32 c_gen_caps = 0;             /* gen_caps entries */
i32 c_gen_quiets = 0;           /* gen_quiets entries */
i32 c_nextmove = 0;             /* next_move entries */
i32 c_isattacked = 0;           /* is_attacked entries */
i32 c_possig = 0;               /* pos_sig entries */
#endif

/* ------------------------------------------------------------------ */
/* make / unmake                                                      */
/* ------------------------------------------------------------------ */

void do_make(Pos *p, u16 m, Undo *u) {
    i16 from = mfrom(m), to = mto(m);
    i16 fl = mfl(m);
    i16 piece = p->board[from];
    i16 promo = ispromo(m) ? (CO(piece) | (fl + 1)) : 0;

    PCOUNT(c_make);

    u->cap = p->board[to];
    u->castle = p->castle;
    u->ep = p->ep;

    p->board[to] = promo ? promo : piece;
    p->board[from] = EMPTY;

    if (fl == MF_CASTLE) {
        if (to == 0x06)      { p->board[0x05] = WR; p->board[0x07] = EMPTY; }
        else if (to == 0x02) { p->board[0x03] = WR; p->board[0x00] = EMPTY; }
        else if (to == 0x76) { p->board[0x75] = BR; p->board[0x77] = EMPTY; }
        else if (to == 0x72) { p->board[0x73] = BR; p->board[0x70] = EMPTY; }
    } else if (fl == MF_EP) {
        if (p->side == 0) p->board[to - 16] = EMPTY;
        else              p->board[to + 16] = EMPTY;
    }

    if (TY(piece) == 6) {
        if (from == 0x04) p->castle &= ~3;
        else if (from == 0x74) p->castle &= ~12;
    }
    if (from == 0x00 || to == 0x00) p->castle &= ~2;
    if (from == 0x07 || to == 0x07) p->castle &= ~1;
    if (from == 0x70 || to == 0x70) p->castle &= ~8;
    if (from == 0x77 || to == 0x77) p->castle &= ~4;

    p->ep = -1;
    if (piece == WP && to == from + 32) p->ep = from + 16;
    else if (piece == BP && to == from - 32) p->ep = from - 16;

    if (TY(piece) == 6) p->ks[p->side] = to;

    zob_apply(p, m, u);          /* sig: old -> new (board/castle/ep already set) */

    p->side ^= 1;

    if (nnue_active) nnue_make(p, m, u);
}

void undo_move(Pos *p, u16 m, Undo *u) {
    i16 from = mfrom(m), to = mto(m);
    i16 fl = mfl(m);
    i16 piece = p->board[to];

    PCOUNT(c_undo);

    p->side ^= 1;

    /* sig: new -> old. Must run before the board/castle/ep restores, while
       board[to] holds the moved piece, board[from] is empty, p->castle/ep are
       still the NEW values and u the OLD - the same state zob_apply saw in
       do_make (after its updates, before its side flip). */
    zob_apply(p, m, u);

    if (ispromo(m)) piece = (p->side == 0) ? WP : BP;
    p->board[from] = piece;
    p->board[to] = u->cap;

    if (fl == MF_CASTLE) {
        if (to == 0x06)      { p->board[0x07] = WR; p->board[0x05] = EMPTY; }
        else if (to == 0x02) { p->board[0x00] = WR; p->board[0x03] = EMPTY; }
        else if (to == 0x76) { p->board[0x77] = BR; p->board[0x75] = EMPTY; }
        else if (to == 0x72) { p->board[0x70] = BR; p->board[0x73] = EMPTY; }
    } else if (fl == MF_EP) {
        if (p->side == 0) p->board[to - 16] = BP;
        else              p->board[to + 16] = WP;
    }

    p->castle = u->castle;
    p->ep = u->ep;
    if (TY(piece) == 6) p->ks[p->side] = from;

    /* NNUE undo restores the accumulator stack (copy-make), board-independent. */
    if (nnue_active) nnue_undo(p);
}

/* ------------------------------------------------------------------ */
/* null move (search-only "pass": same position, opponent to move)    */
/* ------------------------------------------------------------------ */

/* Null-move make/undo: flip the side to move and toggle the Zobrist side key.
   Nothing else changes - the board, castle rights, ep square and the NNUE
   accumulators all stay (the accumulators encode the piece placement, and
   nnue_eval picks the stm accumulator from p->side, so the null-move position
   evaluates correctly with the same accumulators). The two sides are keyed by
   distinct zside[] entries, so XOR of both toggles the side component of the
   signature regardless of the current side; since XOR is its own inverse, make
   and undo are the same operation. */
void nm_make(Pos *p) {
    p->side ^= 1;
    p->sig  ^= zside[0] ^ zside[1];
}
void nm_undo(Pos *p) {
    nm_make(p);
}

/* ------------------------------------------------------------------ */
/* attacks                                                            */
/* ------------------------------------------------------------------ */

/* is the king at ks[s] aligned with sq on a rank/file/diagonal? Used by the
   search's legality check: a NON-king move from a square NOT on the mover
   king's rank/file/diagonal can never open an attack on that king, so the
   is_attacked legality test can be skipped for such moves. */
i16 sq_on_king_line(Pos *p, i16 sq, i16 s) {
    i16 k = p->ks[s];
    i16 kr = k >> 4, kf = k & 7, sr = sq >> 4, sf = sq & 7;
    if (kr == sr) return 1;                          /* same rank */
    if (kf == sf) return 1;                          /* same file */
    return (kr + kf == sr + sf) || (kr - kf == sr - sf);  /* diagonals */
}

i16 is_attacked(Pos *p, i16 sq, i16 by) {
    i16 i, to, d;
    i16 pc;

    PCOUNT(c_isattacked);

    if (by == 0) {
        to = sq - 17; if ((to & 0x88) == 0 && p->board[to] == WP) return 1;
        to = sq - 15; if ((to & 0x88) == 0 && p->board[to] == WP) return 1;
    } else {
        to = sq + 15; if ((to & 0x88) == 0 && p->board[to] == BP) return 1;
        to = sq + 17; if ((to & 0x88) == 0 && p->board[to] == BP) return 1;
    }

    for (i = 0; i < 8; i++) {
        to = sq + kn[i];
        if ((to & 0x88) == 0 && p->board[to] == (by ? BN : WN)) return 1;
    }
    for (i = 0; i < 8; i++) {
        to = sq + ki[i];
        if ((to & 0x88) == 0 && p->board[to] == (by ? BK : WK)) return 1;
    }
    for (i = 0; i < 4; i++) {
        d = rb[i]; to = sq + d;
        while ((to & 0x88) == 0) {
            pc = p->board[to];
            if (pc) {
                if (by ? (pc == BR || pc == BQ) : (pc == WR || pc == WQ)) return 1;
                break;
            }
            to += d;
        }
    }
    for (i = 0; i < 4; i++) {
        d = bb[i]; to = sq + d;
        while ((to & 0x88) == 0) {
            pc = p->board[to];
            if (pc) {
                if (by ? (pc == BB || pc == BQ) : (pc == WB || pc == WQ)) return 1;
                break;
            }
            to += d;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* move generation (pseudo-legal)                                     */
/* ------------------------------------------------------------------ */

/* captures (incl. en passant) + all promotion moves; searched first. Sweeps
   only the 64 on-board 0x88 squares (file 0..7, rank 0..7): the other 64
   entries of board[128] are always EMPTY (off-board), so skipping them halves
   the from-sweep. The on-board squares are visited in ascending order, so the
   move order matches a full 0..127 sweep -> node counts unchanged. After file
   f=7 the next on-board square is the next rank's file 0, i.e. from+9. */
i16 gen_caps(Pos *p, u16 *list) {
    i16 n = 0, from, to, pc, pt, us = p->side, them = us ^ 1;
    i16 i, d, fwd, r;

    PCOUNT(c_gen_caps);

    for (from = 0; from < 128; from += ((from & 7) == 7) ? 9 : 1) {
        pc = p->board[from];
        if (!pc) continue;
        if (CO(pc) != (us ? 8 : 0)) continue;
        pt = TY(pc);

        if (pt == WP || pt == BP) {
            i16 isW = (pc == WP), prank;
            fwd = isW ? 16 : -16;
            r = from >> 4;
            prank = isW ? 6 : 1;   /* row index of from-square for a promoting push */

            /* promoting push to an empty square (all four pieces) */
            if (r == prank) {
                to = from + fwd;
                if ((to & 0x88) == 0 && p->board[to] == EMPTY)
                    for (i = 0; i < 4; i++)
                        list[n++] = MK(to, from, 0, isW ? pw[i] : pb[i]);
            }

            /* captures: diagonal, en passant, promo captures */
            for (d = -1; d <= 1; d += 2) {
                to = from + fwd + d;
                if ((to & 0x88) != 0) continue;
                if (to == p->ep) {
                    list[n++] = MK(to, from, MF_EP, 0);
                } else {
                    i16 tgt = p->board[to];
                    if (tgt && CO(tgt) == (them ? 8 : 0)) {
                        if (r == prank) {
                            for (i = 0; i < 4; i++)
                                list[n++] = MK(to, from, 0, isW ? pw[i] : pb[i]);
                        } else {
                            list[n++] = MK(to, from, 0, 0);
                        }
                    }
                }
            }
        } else if (pt == WN || pt == BN) {
            for (i = 0; i < 8; i++) {
                to = from + kn[i];
                if ((to & 0x88) != 0) continue;
                if (p->board[to] && CO(p->board[to]) != CO(pc))
                    list[n++] = MK(to, from, 0, 0);
            }
        } else if (pt == WK || pt == BK) {
            for (i = 0; i < 8; i++) {
                to = from + ki[i];
                if ((to & 0x88) != 0) continue;
                if (p->board[to] && CO(p->board[to]) != CO(pc))
                    list[n++] = MK(to, from, 0, 0);
            }
        } else {
            const i16 *dirs;
            i16 ndir;
            if (pt == WB || pt == BB)      { dirs = bb; ndir = 4; }
            else if (pt == WR || pt == BR) { dirs = rb; ndir = 4; }
            else                           { dirs = qd; ndir = 8; }
            for (i = 0; i < ndir; i++) {
                d = dirs[i];
                to = from + d;
                while ((to & 0x88) == 0) {
                    i16 tgt = p->board[to];
                    if (tgt) {
                        if (CO(tgt) != CO(pc)) list[n++] = MK(to, from, 0, 0);
                        break;
                    }
                    to += d;
                }
            }
        }
    }
    return n;
}

/* quiet non-capture moves + castling; generated only after caps/killers fail.
   Sweeps only the 64 on-board 0x88 squares (see gen_caps): the off-board
   entries of board[128] are always EMPTY, and the on-board squares are visited
   in ascending order, so the move order matches a full 0..127 sweep -> node
   counts unchanged. */
i16 gen_quiets(Pos *p, u16 *list) {
    i16 n = 0, from, to, to2, pc, pt, us = p->side;
    i16 i, d, fwd, r;

    PCOUNT(c_gen_quiets);

    for (from = 0; from < 128; from += ((from & 7) == 7) ? 9 : 1) {
        pc = p->board[from];
        if (!pc) continue;
        if (CO(pc) != (us ? 8 : 0)) continue;
        pt = TY(pc);

        if (pt == WP || pt == BP) {
            i16 isW = (pc == WP), prank, srank;
            fwd = isW ? 16 : -16;
            r = from >> 4;
            prank = isW ? 6 : 1;   /* row index of from-square for a promoting push */
            srank = isW ? 1 : 6;   /* row index of from-square for a double push */

            if (r != prank) {
                to = from + fwd;
                if ((to & 0x88) == 0 && p->board[to] == EMPTY) {
                    list[n++] = MK(to, from, 0, 0);
                    if (r == srank) {
                        to2 = from + 2 * fwd;
                        if ((to2 & 0x88) == 0 && p->board[to2] == EMPTY)
                            list[n++] = MK(to2, from, 0, 0);
                    }
                }
            }
        } else if (pt == WN || pt == BN) {
            for (i = 0; i < 8; i++) {
                to = from + kn[i];
                if ((to & 0x88) != 0) continue;
                if (!p->board[to])
                    list[n++] = MK(to, from, 0, 0);
            }
        } else if (pt == WK || pt == BK) {
            for (i = 0; i < 8; i++) {
                to = from + ki[i];
                if ((to & 0x88) != 0) continue;
                if (!p->board[to])
                    list[n++] = MK(to, from, 0, 0);
            }
        } else {
            const i16 *dirs;
            i16 ndir;
            if (pt == WB || pt == BB)      { dirs = bb; ndir = 4; }
            else if (pt == WR || pt == BR) { dirs = rb; ndir = 4; }
            else                           { dirs = qd; ndir = 8; }
            for (i = 0; i < ndir; i++) {
                d = dirs[i];
                to = from + d;
                while ((to & 0x88) == 0) {
                    i16 tgt = p->board[to];
                    if (!tgt) {
                        list[n++] = MK(to, from, 0, 0);
                        to += d;
                    } else {
                        break;
                    }
                }
            }
        }
    }

    /* castling (quiet, non-capture) */
    if (us == 0) {
        if ((p->castle & 1) && p->board[0x04] == WK && p->board[0x07] == WR &&
            p->board[0x05] == EMPTY && p->board[0x06] == EMPTY &&
            !is_attacked(p, 0x04, 1) && !is_attacked(p, 0x05, 1) && !is_attacked(p, 0x06, 1))
            list[n++] = MK(0x06, 0x04, MF_CASTLE, 0);
        if ((p->castle & 2) && p->board[0x04] == WK && p->board[0x00] == WR &&
            p->board[0x01] == EMPTY && p->board[0x02] == EMPTY && p->board[0x03] == EMPTY &&
            !is_attacked(p, 0x04, 1) && !is_attacked(p, 0x03, 1) && !is_attacked(p, 0x02, 1))
            list[n++] = MK(0x02, 0x04, MF_CASTLE, 0);
    } else {
        if ((p->castle & 4) && p->board[0x74] == BK && p->board[0x77] == BR &&
            p->board[0x75] == EMPTY && p->board[0x76] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x75, 0) && !is_attacked(p, 0x76, 0))
            list[n++] = MK(0x76, 0x74, MF_CASTLE, 0);
        if ((p->castle & 8) && p->board[0x74] == BK && p->board[0x70] == BR &&
            p->board[0x71] == EMPTY && p->board[0x72] == EMPTY && p->board[0x73] == EMPTY &&
            !is_attacked(p, 0x74, 0) && !is_attacked(p, 0x73, 0) && !is_attacked(p, 0x72, 0))
            list[n++] = MK(0x72, 0x74, MF_CASTLE, 0);
    }
    return n;
}

/* full pseudo-legal list = captures + quiets (perft/protocol still use this) */
i16 gen_moves(Pos *p, u16 *list) {
    i16 n = gen_caps(p, list);

    PCOUNT(c_gen_moves);
    return n + gen_quiets(p, list + n);
}

/* ------------------------------------------------------------------ */
/* staged move generator                                              */
/* ------------------------------------------------------------------ */

#define MG_TT      0   /* transposition-table move (empty for now) */
#define MG_CAPS    1   /* captures/promotions, MVV-LVA selection */
#define MG_KILLERS 2   /* two quiet killers */
#define MG_QUIETS  3   /* remaining quiets (as generated) */
#define MG_DONE    4

#define PROMO_BONUS 16000   /* promotions sort ahead of every capture; int16-safe */

/* MVV-LVA: victim material * 16 - attacker type. Promotions get a bonus.
   The score depends ONLY on the (victim type, attacker type) pair, so a
   tiny 8x8 table (128 B) precomputes it once instead of a per-move score
   buffer or the mval*16 arithmetic on every selection pass. */
static i16 mvv_tab[8][8];

static void mvv_build(void) {
    i16 v, a;
    for (v = 0; v < 8; v++)
        for (a = 0; a < 8; a++)
            mvv_tab[v][a] = mval[v] * 16 - a;
}

static i16 mvv_lva(Pos *p, u16 m) {
    i16 victim = 0, s;
    if (mfl(m) == MF_EP) victim = 1;                       /* captured pawn */
    else if (p->board[mto(m)]) victim = TY(p->board[mto(m)]);
    s = mvv_tab[victim][TY(p->board[mfrom(m)])];
    if (ispromo(m)) s += PROMO_BONUS;
    return s;
}

/* killer sanity: the move must be a genuine quiet pseudo-legal move of the
   side to move. A bare "from occupied, to empty" check is not enough: a
   killer recorded in another branch may point at a square that now holds a
   different piece (even a king), and making that move corrupts ks[]/board. */
static i16 quiet_killer_ok(Pos *p, u16 m) {
    i16 from = mfrom(m), to = mto(m);
    i16 piece = p->board[from];
    i16 pt, i, d, diff;

    if (!piece) return 0;
    if (CO(piece) != (p->side ? 8 : 0)) return 0;   /* must be our piece */
    if (mfl(m) != 0) return 0;                      /* killers are flag-0 quiets */
    if (p->board[to]) return 0;                     /* non-capture */

    pt = TY(piece);
    if (pt == 1) {
        i16 fwd = (piece == WP) ? 16 : -16;
        if (to == from + fwd) return 1;
        if (to == from + 2 * fwd)
            return (piece == WP) ? ((from >> 4) == 1) : ((from >> 4) == 6);
        return 0;
    }
    if (pt == 2) {
        for (i = 0; i < 8; i++)
            if (to == from + kn[i]) return 1;
        return 0;
    }
    if (pt == 6) {
        for (i = 0; i < 8; i++)
            if (to == from + ki[i]) return 1;
        return 0;
    }
    if (pt == 3 || pt == 4 || pt == 5) {
        const i16 *dirs;
        i16 ndir;
        if (pt == 3)      { dirs = bb; ndir = 4; }
        else if (pt == 4) { dirs = rb; ndir = 4; }
        else              { dirs = qd; ndir = 8; }
        for (i = 0; i < ndir; i++) {
            d = dirs[i];
            for (diff = from + d; (diff & 0x88) == 0; diff += d) {
                if (diff == to) return 1;
                if (p->board[diff]) break;          /* blocked before reaching to */
            }
        }
    }
    return 0;
}

void mgen_init(Pos *p, MGen *g, i16 ply, u16 k0, u16 k1, u16 ttm) {
    (void)p;
    g->list = movebuf[ply];
    g->n = 0;
    g->idx = 0;
    g->stage = ttm ? MG_TT : MG_CAPS;   /* skip the TT stage when no ttm */
    g->ttm = ttm;
    g->k0 = k0;
    g->k1 = k1;
    g->caps_only = 0;
}

/* quiescence init: captures only (MVV-LVA), no killers/quiets */
void mgen_init_q(Pos *p, MGen *g, i16 ply) {
    (void)p;
    g->list = movebuf[ply];
    g->n = 0;
    g->idx = 0;
    g->stage = MG_CAPS;
    g->ttm = 0;
    g->k0 = g->k1 = 0;
    g->caps_only = 1;
}

u16 next_move(Pos *p, MGen *g) {
    u16 m;
    i16 i, best, bscore;

    PCOUNT(c_nextmove);

again:
    switch (g->stage) {
    case MG_TT:
        g->stage = MG_CAPS;
        if (g->ttm) return g->ttm;
        goto again;

    case MG_CAPS:
        if (g->idx == 0) g->n = gen_caps(p, g->list);
        while (g->idx < g->n) {
            best = -1; bscore = -32000;
            for (i = g->idx; i < g->n; i++) {
                i16 s;
                if (g->list[i] == g->ttm) continue;       /* already tried */
                s = mvv_lva(p, g->list[i]);
                if (s > bscore) { bscore = s; best = i; }
            }
            if (best < 0) break;
            m = g->list[best]; g->list[best] = g->list[g->idx]; g->list[g->idx] = m;
            g->idx++;
            return m;
        }
        if (g->caps_only) { g->stage = MG_DONE; return 0; }   /* quiescence */
        g->stage = MG_KILLERS; g->idx = 0;
        goto again;

    case MG_KILLERS:
        if (g->idx < 2) {
            u16 k = (g->idx == 0) ? g->k0 : g->k1;
            g->idx++;
            if (k && k != g->ttm && quiet_killer_ok(p, k)) return k;
            goto again;
        }
        g->stage = MG_QUIETS; g->idx = 0;
        goto again;

    case MG_QUIETS:
        if (g->idx == 0) g->n = gen_quiets(p, g->list);
        while (g->idx < g->n) {
            best = -1; bscore = -32000;
            for (i = g->idx; i < g->n; i++) {
                u16 q = g->list[i];
                i16 s;
                if (q == g->ttm || q == g->k0 || q == g->k1) continue;  /* already tried */
                s = qhist[p->side][TY(p->board[mfrom(q)]) - 1][(q >> 6) & 0x3F];
                if (s > bscore) { bscore = s; best = i; }
            }
            if (best < 0) break;                        /* only already-tried left */
            m = g->list[best]; g->list[best] = g->list[g->idx]; g->list[g->idx] = m;
            g->idx++;
            return m;
        }
        g->stage = MG_DONE;
        return 0;

    default:
        return 0;
    }
}

/* debug: staged generator must yield exactly the gen_moves set, once each */
static int moveset_check(Pos *p) {
    u16 all[256], got[256];
    MGen mg;
    u16 m;
    i16 n, i, j, na = 0;
    i16 bad = 0;

    n = gen_moves(p, all);
    mgen_init(p, &mg, 0, 0, 0, 0);
    while ((m = next_move(p, &mg)) != 0) got[na++] = m;

    printf("gen_moves=%d next_move=%d\n", n, na);
    if (n != na) { printf("COUNT MISMATCH\n"); return 1; }
    for (i = 0; i < n; i++) {
        for (j = 0; j < na; j++)
            if (got[j] == all[i]) break;
        if (j == na) { printf("MISSING %04X\n", (unsigned)all[i]); bad = 1; }
    }
    for (i = 0; i < na; i++)
        for (j = i + 1; j < na; j++)
            if (got[i] == got[j]) { printf("DUP %04X\n", (unsigned)got[i]); bad = 1; }
    printf(bad ? "MOVE SET DIFF\n" : "move set OK\n");
    return bad;
}

/* ------------------------------------------------------------------ */
/* perft                                                              */
/* ------------------------------------------------------------------ */

i32 perft(Pos *p, i16 depth) {
    i32 nodes = 0;
    u16 *list = movebuf[depth];
    i16 n = gen_moves(p, list);
    i16 i;

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

/* ------------------------------------------------------------------ */
/* evaluation (NNUE when a net is loaded, else material)              */
/* ------------------------------------------------------------------ */

Score evaluate(Pos *p) {
    Score score = 0;
    i16 sq;
    if (nnue_enabled && nnue_active) return nnue_eval(p);
    for (sq = 0; sq < 128; sq++) {
        i16 pc = p->board[sq];
        if (!pc) continue;
        if (CO(pc) == 0) score += mval[TY(pc)];
        else             score -= mval[TY(pc)];
    }
    return (p->side == 0) ? score : -score;   /* negamax: side to move */
}

/* ------------------------------------------------------------------ */
/* position signature (for repetition)                                */
/* ------------------------------------------------------------------ */

/* O(1): the signature is maintained incrementally in do_make/undo_move and
   computed from scratch only at parse_fen (zob_compute). The from-scratch
   FNV-1a over 128 squares is gone (was 9,678 c286 per node). */
Sig pos_sig(Pos *p) {
    PCOUNT(c_possig);
    return p->sig;
}

/* ------------------------------------------------------------------ */
/* FEN                                                                */
/* ------------------------------------------------------------------ */

static i16 pchar(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
    }
    return EMPTY;
}

void parse_fen(Pos *p, const char *s) {
    i16 i, rank = 7, file = 0;

    for (i = 0; i < 128; i++) p->board[i] = EMPTY;
    p->ks[0] = p->ks[1] = -1;

    while (*s && *s != ' ') {
        char c = *s++;
        if (c == '/') { rank--; file = 0; }
        else if (c >= '1' && c <= '8') file += c - '0';
        else { p->board[rank * 16 + file] = pchar(c); file++; }
    }
    s++;
    p->side = (*s == 'b') ? 1 : 0;
    s += 2;

    p->castle = 0;
    while (*s && *s != ' ') {
        char c = *s++;
        if (c == 'K') p->castle |= 1;
        else if (c == 'Q') p->castle |= 2;
        else if (c == 'k') p->castle |= 4;
        else if (c == 'q') p->castle |= 8;
    }
    s++;

    p->ep = -1;
    while (*s && *s != ' ') {
        char c = *s++;
        if (c >= 'a' && c <= 'h') {
            i16 f = c - 'a';
            if (*s >= '1' && *s <= '8')
                p->ep = ((*s) - '1') * 16 + f;
        }
    }

    g_half = 0; g_full = 1;
    if (*s) {
        i16 h = 0, f = 1;
        if (sscanf(s, "%hd %hd", &h, &f) >= 1) { g_half = h; g_full = f; }
    }

    for (i = 0; i < 128; i++) {
        if (p->board[i] == WK) p->ks[0] = i;
        else if (p->board[i] == BK) p->ks[1] = i;
    }

    zob_compute(p);   /* incremental signature must start from this position */
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static const char *fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
};

static const u32 expv[6][6] = {
    { 20, 400, 8902, 197281, 4865609, 119060324 },
    { 48, 2039, 97862, 4085603, 193690690, 0 },
    { 14, 191, 2812, 43238, 674624, 11030083 },
    { 6, 264, 9467, 422333, 15833292, 706045033 },
    { 44, 1486, 62379, 2103487, 89941194, 0 },
    { 46, 2079, 89890, 3894594, 164075551, 0 }
};

int main(int argc, char **argv) {
    i16 maxd = 5, test = 0, i, splitsel = 0;
    i16 j, nn_log = 1;
    Pos pos;
    i32 nodes;
    clock_t t0, t1;
    double secs;

    mvv_build();   /* one-time MVV-LVA table (dedicated init) */
    zob_init();    /* one-time Zobrist key tables (dedicated init) */
    lmr_build();   /* one-time LMR log table (dedicated init) */

    /* NNUE net: `chess --nnue file ...` overrides the default net. The default is
       the EMBEDDED net on the gcc build (OpenBench embeds the trained net via
       EVALFILE); on the 16-bit build it is the chess.net file. NNUE is the
       default; a load failure is loud, not a silent material-eval fallback. */
    if (argc > 2 && strcmp(argv[1], "--nnue") == 0) {
        if (!nnue_load(argv[2])) printf("NNUE: load failed: %s\n", argv[2]);
        for (j = 2; j < argc; j++) argv[j - 2] = argv[j];
        argc -= 2;
    } else {
        if (!nnue_ensure_default())
            printf("NNUE: net load failed - running material eval "
                   "(no embedded net on this build / no chess.net)\n");
    }
    if (argc == 1 || (argc > 1 && strcmp(argv[1], "xboard") == 0)) nn_log = 0;
    if (nn_log && nnue_enabled)
        printf("NNUE: net loaded (features=%d N=%d)\n", NNUE_FEATURES, NNUE_N);

    if (argc > 1 && strcmp(argv[1], "nn") == 0)
        return nnue_selftest((argc > 2) ? argv[2] : NULL);

    if (argc > 1 && strcmp(argv[1], "nbench") == 0)
        return nnue_bench();

    if (argc > 1 && strcmp(argv[1], "sbench") == 0)
        return sbench();

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "xboard") == 0))
        return xboard_main();

    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        int bd = 0;                     /* 0 = BENCH_DEPTH default */
        if (argc > 2) bd = atoi(argv[2]);
        return bench(bd);
    }

#ifdef PROFILE
    if (argc > 1 && strcmp(argv[1], "profile") == 0)
        return profile((argc > 2) ? atoi(argv[2]) : 4);
#endif

    if (argc > 1 && argv[1][0] == 'm') {
        Pos q;
        parse_fen(&q, (argc > 2) ? argv[2] : fens[0]);
        return moveset_check(&q);
    }

    if (argc > 1 && argv[1][0] == 's') {
        int s_depth = 5;
        test = 0;
        if (argc > 2) s_depth = atoi(argv[2]);
        if (argc > 3) test = atoi(argv[3]);
        if (s_depth < 1) s_depth = 1;
        if (s_depth > 10) s_depth = 10;
        if (test == 8) {
            FILE *f = fopen("fen.txt", "r");
            char buf[128];
            if (f) {
                fgets(buf, 128, f);
                fclose(f);
                parse_fen(&pos, buf);
            } else {
                parse_fen(&pos, fens[0]);
            }
        } else if (test == 9 && argc > 4) {
            parse_fen(&pos, argv[4]);
        } else if (test >= 0 && test <= 5) {
            parse_fen(&pos, fens[test]);
        } else {
            parse_fen(&pos, fens[0]);
        }
        search_root(&pos, s_depth);
        return 0;
    }

    if (argc > 1) maxd = atoi(argv[1]);
    if (argc > 2) test = atoi(argv[2]);
    if (argc > 3 && argv[3][0] == 's') splitsel = 1;
    if (maxd < 1) maxd = 1;
    if (maxd > 6) maxd = 6;

    if (test == 9 && argc > 4)
        parse_fen(&pos, argv[4]);
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
    } else if (test >= 0 && test <= 5)
        parse_fen(&pos, fens[test]);
    else
        parse_fen(&pos, fens[0]);

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
                c = perft(&pos, maxd - 1);
            else
                c = -1;
            undo_move(&pos, list[i], &u);
            printf("m=%02X%02X p=%d -> %lu\n",
                   mfrom(list[i]), mto(list[i]), mpro(list[i], us ? 8 : 0), (unsigned long)c);
        }
        return 0;
    }

    for (i = 1; i <= maxd; i++) {
        u32 want, got;
        t0 = clock();
        nodes = perft(&pos, i);
        t1 = clock();
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        got = (u32)nodes;
        want = expv[test][i - 1];
        printf("perft(%d)=%lu  %8.2fs  %6lu nps  %s\n",
               i, (unsigned long)got, secs,
               secs > 0 ? (unsigned long)((double)nodes / secs) : 0UL,
               (want && got != want) ? "*** WRONG ***" : "ok");
    }
    return 0;
}

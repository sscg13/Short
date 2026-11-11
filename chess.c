/* chess.c - board, move generation, make/unmake, eval, FEN, main */

#include "engine.h"

#ifdef MAME_MARKERS
#include <conio.h>
void mame_mark(u8 id) { outp(0x00e9, id); }
#endif

static const i16 kn[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const i16 ki[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 rb[4] = { -16, 1, 16, -1 };
static const i16 bb[4] = { -17, -15, 15, 17 };
static const i16 qd[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 pw[4] = { WN, WB, WR, WQ };
static const i16 pb[4] = { BN, BB, BR, BQ };
static const i16 mval[8] = { 0, 100, 320, 330, 500, 900, 0 };

u16 movebuf[32][256];

/* Quiet-ordering scratch: the history score of every candidate in the list
   being sorted. next_move scores a quiet list once and sorts it in full
   before returning its first move, so these scores are dead before any
   descendant can start a list of its own: one 512-byte row covers the whole
   search. (The old code re-read the history matrix on every scan, which is
   what this replaces.) */
static i16 qscore[256];

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
/* The side transition is identical in both directions. Keep its four words
   near so every move needs one XOR instead of two far-table reads. */
static Sig zside_delta;

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
#include "repetition.inc"

void zob_init(void) {
    i16 pc, sq, i;
    for (pc = 1; pc < 15; pc++)
        for (sq = 0; sq < 64; sq++)
            zpsq[pc][sq] = zkey();
    zside[0] = zkey(); zside[1] = zkey();
    zside_delta = zside[0] ^ zside[1];
    for (i = 0; i < 16; i++) zcastle[i] = zkey();
    for (i = 0; i < 65; i++) zep[i] = zkey();
    rep_table_init();
}

/* ep-square key index: -1 (no ep) -> 64, else compact square */
#define ZEPI(ep) ((ep) < 0 ? 64 : (i16)sq2c(ep))

/* XOR the move's signature delta into p->sig after board/castle/ep updates,
   before the side flip. board[to] holds the moved piece, board[from] is empty,
   p->castle/ep hold the NEW values and u the OLD. Undo restores u->sig. */
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
    /* Equal old/new state keys cancel. Most moves change neither component. */
    if (u->castle != p->castle) {
        p->sig ^= zcastle[u->castle];
        p->sig ^= zcastle[p->castle];
    }
    if (u->ep != p->ep) {
        p->sig ^= zep[ZEPI(u->ep)];
        p->sig ^= zep[ZEPI(p->ep)];
    }
    p->sig ^= zside_delta;
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
i32 c_nm_tt = 0;                /* next_move: TT-stage returns */
i32 c_nm_capscan = 0;           /* next_move: capture-stage candidate comparisons */
i32 c_nm_qscan = 0;             /* next_move: quiet-stage score computations */
i32 c_nm_qsort = 0;             /* next_move: quiet-stage sort comparisons */
i32 c_nm_qret = 0;              /* next_move: quiet-stage moves returned */
i32 c_nm_killer = 0;            /* next_move: killer validations */
i32 c_isattacked = 0;           /* is_attacked entries */
i32 c_possig = 0;               /* pos_sig entries */
i32 c_see = 0, c_see_step = 0;  /* SEE entries and attacker scans */
#endif

/* ------------------------------------------------------------------ */
/* make / unmake                                                      */
/* ------------------------------------------------------------------ */

/* Castling-rights mask per 0x88 square: 0xFF except the six squares whose
   vacating or occupation changes a right.  The old code tested eight squares
   on EVERY move (`from`/`to` versus the four rook corners, plus a piece-type
   test for the king start squares) and did four guarded read-modify-writes;
   two indexed ANDs replace all of it.

   The table is a superset of the old piece-conditional king test: it drops a
   right when any piece leaves or reaches e1/e8, not only a king.  That is
   unreachable with the right still live - e1 can only be vacant, or hold the
   other side's piece, after the king has already moved away and lost it - and
   the operation is idempotent, so the position stream is unchanged. */
u8 cast_mask[128];

static void cast_mask_build(void) {
    i16 sq;
    for (sq = 0; sq < 128; sq++) cast_mask[sq] = 0xFF;
    cast_mask[0x04] = (u8)~3;    /* e1 king moves: white castles */
    cast_mask[0x74] = (u8)~12;   /* e8 king moves: black castles */
    cast_mask[0x00] = (u8)~2;    /* a1: white queenside */
    cast_mask[0x07] = (u8)~1;    /* h1: white kingside  */
    cast_mask[0x70] = (u8)~8;    /* a8: black queenside */
    cast_mask[0x77] = (u8)~4;    /* h8: black kingside  */
}

void do_make(Pos *p, u16 m, Undo *u) {
    i16 from = mfrom(m), to = mto(m);
    i16 fl = mfl(m);
    i16 piece = p->board[from];
    i16 promo = ispromo(m) ? (CO(piece) | (fl + 1)) : 0;

    PCOUNT(c_make);

    u->cap = p->board[to];
    u->castle = p->castle;
    u->ep = p->ep;
    u->sig = p->sig;

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

    p->castle &= cast_mask[from] & cast_mask[to];

    /* En passant belongs to the position only while the side to move has a
       pawn that could capture there (FIDE 9.2.2): a double push nobody can
       answer leaves the position unchanged, so it must not change the
       signature. Canonicalise the FIELD, not the hash delta: a capturing pawn
       that is itself the move would make the old target look irrelevant after
       the fact, and zob_apply would then XOR out a key that was never set.
       The capturers stand beside the destination, on the rank the pushed pawn
       just landed on; those squares are untouched by the push, so this reads
       the same before and after the board update. */
    p->ep = -1;
    if (piece == WP && to == from + 32) {
        if (p->board[to - 1] == BP || p->board[to + 1] == BP) p->ep = to - 16;
    } else if (piece == BP && to == from - 32) {
        if (p->board[to - 1] == WP || p->board[to + 1] == WP) p->ep = to + 16;
    }

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

    p->sig = u->sig;

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

/* A pass expires en passant: retaining the target would let the wrong side
   capture there. Save it in the caller's frame so nested nulls undo correctly.
   Piece placement is unchanged, so the NNUE accumulators remain valid. */
i16 nm_make(Pos *p) {
    i16 ep = p->ep;
    if (ep >= 0) {
        p->sig ^= zep[ZEPI(ep)];
        p->sig ^= zep[64];
        p->ep = -1;
    }
    p->side ^= 1;
    p->sig  ^= zside_delta;
    return ep;
}
void nm_undo(Pos *p, i16 ep) {
    p->side ^= 1;
    p->sig  ^= zside_delta;
    if (ep >= 0) {
        p->sig ^= zep[64];
        p->sig ^= zep[ZEPI(ep)];
        p->ep = ep;
    }
}

/* ------------------------------------------------------------------ */
/* attacks                                                            */
/* ------------------------------------------------------------------ */

#ifdef ASM_ISATTACKED
/* -AsmAttack build: the reference body lives in attack_ref.c (is_attacked_c)
   and the hot path is attack_opt.asm.  Both are linked so the `atkdiff`
   selftest can compare them; this translation unit keeps no copy. */
i16 is_attacked(Pos *p, i16 sq, i16 by) {
    PCOUNT(c_isattacked);
    return is_attacked_asm(p, sq, by);
}
#else
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

    /* Same probe order, with immediate offsets and one color choice. */
    pc = by ? BN : WN;
#define ATTACK_KNIGHT(offset) do { \
        to = sq + (offset); \
        if ((to & 0x88) == 0 && p->board[to] == pc) return 1; \
    } while (0)
    ATTACK_KNIGHT(-33);
    ATTACK_KNIGHT(-31);
    ATTACK_KNIGHT(-18);
    ATTACK_KNIGHT(-14);
    ATTACK_KNIGHT(14);
    ATTACK_KNIGHT(18);
    ATTACK_KNIGHT(31);
    ATTACK_KNIGHT(33);
#undef ATTACK_KNIGHT
    /* Both king squares are maintained by make/undo. Bit 1 of king_line
       marks adjacency, so a single difference lookup replaces eight probes.
       The board check also handles a missing/captured king in diagnostic FENs. */
    to = p->ks[by];
    if (to >= 0 && p->board[to] == (by ? BK : WK) &&
        (king_line[(u8)(to - sq)] & 2)) return 1;
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
#endif /* ASM_ISATTACKED */

/* Test a recapture using board-only changes: no Zobrist or NNUE work. This
   rejects pinned attackers and unsafe king captures, including pins revealed
   by earlier exchanges. The caller's position is restored before returning. */
static i16 see_legal(Pos *p, i16 from, i16 to, i16 side) {
    i16 pc = p->board[from], cap = p->board[to], king = p->ks[side];
    i16 legal;
    p->board[from] = EMPTY;
    p->board[to] = pc;
    legal = !is_attacked(p, TY(pc) == 6 ? to : king, side ^ 1);
    p->board[from] = pc;
    p->board[to] = cap;
    return legal;
}

/* Least valuable LEGAL attacker of to. Probe outwards from the destination
   instead of sweeping the whole board. Re-scanning after each removal exposes
   sliding x-rays without a bitboard, piece list, or persistent scratch data. */
static i16 see_lva(Pos *p, i16 to, i16 side) {
    i16 i, from, pc, type, best = -1, value = 1000;
    i16 col = side ? 8 : 0, pawn = side ? BP : WP;
    i16 back = side ? 16 : -16;
    PCOUNT(c_see_step);
    from = to + back - 1;
    if (!(from & 0x88) && p->board[from] == pawn && see_legal(p, from, to, side))
        return from;
    from = to + back + 1;
    if (!(from & 0x88) && p->board[from] == pawn && see_legal(p, from, to, side))
        return from;
    /* Constant origins remove the eight-probe loop's indexing overhead.
       Keep precisely the kn[] order so equal-value attacker ties agree. */
#define SEE_KN(delta) \
    from = to + (delta); \
    if (!(from & 0x88) && p->board[from] == (col | 2) && \
        see_legal(p, from, to, side)) return from;
    SEE_KN(-33)
    SEE_KN(-31)
    SEE_KN(-18)
    SEE_KN(-14)
    SEE_KN(14)
    SEE_KN(18)
    SEE_KN(31)
    SEE_KN(33)
#undef SEE_KN
    for (i = 0; i < 8; ++i) {
        from = to + qd[i];
        while (!(from & 0x88) && !p->board[from]) from += qd[i];
        if (from & 0x88) continue;
        pc = p->board[from]; type = TY(pc);
        if (CO(pc) != col || type < 3 || type > 5) continue;
        if (type != 5 && type != ((qd[i] == -16 || qd[i] == -1 ||
                                  qd[i] == 1 || qd[i] == 16) ? 4 : 3)) continue;
        if (mval[type] < value && see_legal(p, from, to, side)) {
            best = from; value = mval[type];
        }
    }
    if (best >= 0) return best;
    from = p->ks[side];
    if (from >= 0 && p->board[from] == (col | 6) &&
        (king_line[(u8)(from - to)] & 2) && see_legal(p, from, to, side))
        return from;
    return -1;
}

/* Threshold static exchange evaluation for ordinary captures and quiets. The balance
   alternates between sides; once the next capture cannot change the sign, stop.
   Equality with the threshold passes. Only removed pieces need saving (128 bytes at most),
   and all arithmetic fits in a signed 16-bit word. Special moves pass; a pawn
   recapturing onto its promotion rank also passes conservatively.
   This estimates a least-attacker exchange, not the value of a tactical move. */
i16 see_ge(Pos *p, u16 m, i16 threshold) {
    i16 squares[32], pieces[32];
    i16 from = mfrom(m), to = mto(m), pc = p->board[from];
    i16 cap = p->board[to], balance, side, result = 1, n = 1;
    PCOUNT(c_see);
    MAME_MARK(0x60);
    if (mfl(m) != 0 || TY(pc) == 6) { MAME_MARK(0x61); return 1; }
    balance = mval[TY(cap)] - threshold;
    if (balance < 0) { MAME_MARK(0x61); return 0; }
    balance = mval[TY(pc)] - balance;
    if (balance <= 0) { MAME_MARK(0x61); return 1; }
    squares[0] = from; pieces[0] = pc;
    p->board[from] = EMPTY; p->board[to] = pc;
    side = p->side ^ 1;
    while ((from = see_lva(p, to, side)) >= 0) {
        pc = p->board[from];
        if (TY(pc) == 1 && (to >> 4) == (side ? 0 : 7)) {
            result = 1; break;
        }
        result ^= 1;
        if (TY(pc) == 6) break;    /* legal king recapture ends the exchange */
        balance = mval[TY(pc)] - balance;
        if (balance < result) break;
        if (n == 32) { result = 1; break; } /* malformed diagnostic FEN: keep move */
        squares[n] = from; pieces[n++] = pc;
        p->board[from] = EMPTY; p->board[to] = pc;
        side ^= 1;
    }
    p->board[to] = cap;
    while (n > 0) { --n; p->board[squares[n]] = pieces[n]; }
    MAME_MARK(0x61);
    return result;
}

/* ------------------------------------------------------------------ */
/* move generation (pseudo-legal)                                     */
/* ------------------------------------------------------------------ */

#ifdef ASM_GENCAPS
/* -AsmGenCaps build: the capture generator lives in gen_opt.asm and the
   reference body in gen_ref.c (test only), exactly like is_attacked.

   _mf[sq] = sq2c(sq) and _mt[sq] = sq2c(sq) << 6 let the asm encode a move
   with two indexed loads and an OR instead of the shift/mask/or sequence.
   Both are indexed by the 0x88 square (0..127); off-board squares are never
   pieces, so their entries only have to be well defined, not meaningful. */
u16 mf[128];
u16 mt[128];

static void move_tables_build(void) {
    i16 sq;
    for (sq = 0; sq < 128; sq++) {
        u16 c = (u16)sq2c(sq);
        mf[sq] = c;
        mt[sq] = (u16)(c << 6);
    }
}

i16 gen_caps(Pos *p, u16 *list) {
    PCOUNT(c_gen_caps);
    return gen_caps_asm(p, list);
}
#else

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

    /* Branchless next on-board 0x88 square: within a rank, (from+9)&~8
       clears the added 8 and advances by one; after file h it advances to
       the next rank's file a. */
    for (from = 0; from < 128; from = (from + 9) & ~8) {
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
#endif /* ASM_GENCAPS */

/* quiet non-capture moves + castling; generated only after caps/killers fail.
   Sweeps only the 64 on-board 0x88 squares (see gen_caps): the off-board
   entries of board[128] are always EMPTY, and the on-board squares are visited
   in ascending order, so the move order matches a full 0..127 sweep -> node
   counts unchanged. */
i16 gen_quiets(Pos *p, u16 *list) {
    i16 n = 0, from, to, to2, pc, pt, us = p->side;
    i16 i, d, fwd, r;

    PCOUNT(c_gen_quiets);

    for (from = 0; from < 128; from = (from + 9) & ~8) {
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
u8 king_line[256];

static void mvv_build(void) {
    i16 v, a;
    for (v = 0; v < 8; v++)
        for (a = 0; a < 8; a++)
            mvv_tab[v][a] = mval[v] * 16 - a;
}

static void king_line_build(void) {
    i16 k, sq;
    /* Precompute the king-line relation used by the search legality fast
       path.  Valid 0x88-square differences span -119..119, so their u8
       encodings are unique and a single byte lookup is exact. */
    memset(king_line, 0, sizeof king_line);
    for (k = 0; k < 128; k += ((k & 7) == 7) ? 9 : 1) {
        i16 kr = k >> 4, kf = k & 7;
        for (sq = 0; sq < 128; sq += ((sq & 7) == 7) ? 9 : 1) {
            i16 sr = sq >> 4, sf = sq & 7;
            if (kr == sr || kf == sf || kr + kf == sr + sf || kr - kf == sr - sf)
                king_line[(u8)(sq - k)] |= 1;
            if (sq != k && sr - kr >= -1 && sr - kr <= 1 &&
                sf - kf >= -1 && sf - kf <= 1)
                king_line[(u8)(sq - k)] |= 2;
        }
    }
}

/* MVV-LVA capture score, inlined at its call sites. next_move makes 93,314
   comparisons per bench-4 run; as a function that was a real far call each
   time (512 cycles/step on the 80186 vs 274 for the inlined quiet step, see
   artifacts/orderbench-2026-11-07). Score and order are unchanged: same
   table, same victim/promotion rules. mv is a register-resident move word at
   both call sites, so the repeated subexpressions cost nothing. */
#define MVV_LVA(p, mv) \
    (mvv_tab[(mfl(mv) == MF_EP) ? 1 : \
             ((p)->board[mto(mv)] ? TY((p)->board[mto(mv)]) : 0)] \
            [TY((p)->board[mfrom(mv)])] + (ispromo(mv) ? PROMO_BONUS : 0))

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
            return p->board[from + fwd] == EMPTY &&
                   ((piece == WP) ? ((from >> 4) == 1) : ((from >> 4) == 6));
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

/* Sort a quiet list by frozen history score, descending, moving the score row
   with the moves. Insertion sort is stable and, because most quiet scores tie
   at the same value, usually stops after one comparison per move; that is much
   cheaper than a full selection scan. */
static void sort_quiets(u16 *list, i16 *score, i16 n) {
    i16 i, j, s;
    u16 m;
    for (i = 1; i < n; i++) {
        m = list[i]; s = score[i];
        j = i;
        while (j > 0) {
            PCOUNT(c_nm_qsort);
            if (score[j - 1] >= s) break;
            score[j] = score[j - 1];
            list[j] = list[j - 1];
            j--;
        }
        list[j] = m; score[j] = s;
    }
}

u16 next_move(Pos *p, MGen *g) {
    u16 m;
    i16 i, best, bscore;
#if defined(__WATCOMC__) && defined(__I86__)
    i16 (__near *history)[6][64];
#else
    i16 (*history)[6][64];
#endif

    PCOUNT(c_nextmove);

again:
    switch (g->stage) {
    case MG_TT:
        g->stage = MG_CAPS;
        if (g->ttm) { PCOUNT(c_nm_tt); return g->ttm; }
        goto again;

    case MG_CAPS:
        if (g->idx == 0) g->n = gen_caps(p, g->list);
        while (g->idx < g->n) {
            best = -1; bscore = -32000;
            for (i = g->idx; i < g->n; i++) {
                u16 mv = g->list[i];
                i16 s;
                if (mv == g->ttm) continue;               /* already tried */
                PCOUNT(c_nm_capscan);
                s = MVV_LVA(p, mv);
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
            if (k && k != g->ttm) {
                PCOUNT(c_nm_killer);
                if (quiet_killer_ok(p, k)) return k;
            }
            goto again;
        }
        g->stage = MG_QUIETS; g->idx = 0;
        goto again;

    case MG_QUIETS:
        if (g->idx == 0) {
            g->n = gen_quiets(p, g->list);
            /* Score every candidate once, then sort the list. Descendants
               still update qhist on a cutoff, but this list keeps the scores
               it started with; the old code re-read the 768-byte matrix on
               every scan, which was 13.8 history lookups per node. */
#if defined(__WATCOMC__) && defined(__I86__)
            history = (i16 (__near *)[6][64]) &qhist[p->side];
#else
            history = &qhist[p->side];
#endif
            for (i = 0; i < g->n; i++) {
                u16 q = g->list[i];
                PCOUNT(c_nm_qscan);
                qscore[i] = (*history)[TY(p->board[mfrom(q)]) - 1][(q >> 6) & 0x3F];
            }
            sort_quiets(g->list, qscore, g->n);
        }
        while (g->idx < g->n) {
            u16 q = g->list[g->idx++];
            if (q == g->ttm || q == g->k0 || q == g->k1) continue;  /* already tried */
            PCOUNT(c_nm_qret);
            return q;
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

/* Price next_move's scan steps in isolation (`chess orderbench`). next_move is
   a selection sort over the generated list, so its cost is per candidate:
   capture candidates pay the ttm skip test + mvv_lva + the best-so-far test,
   quiet candidates the three skip tests + the history-matrix lookup + the
   best-so-far test. The loops below use next_move's exact expressions on a
   real move list, so the compiled step cost matches the search's. Cycle costs
   come from the Nimbus port-E9 markers: 0x30/0x31 capture steps, 0x32/0x33
   quiet steps, 0x34/0x35 killer validation. The ms figures are a cross-check
   only: the Nimbus BIOS tick timer is dead (they print 0 there); on 86Box the
   engine clock resolves at about 1 ms. */
#ifdef MAME_MARKERS
static u16 ob_ttm, ob_k0, ob_k1;   /* mutable: keep the skip tests from folding */
static i16 ob_score[256];          /* frozen quiet scores (near, like qscore) */

int order_bench(void) {
    static Pos pos;
    static u16 caps[256], quiets[256];
    u16 list[256];
    i16 n, nc = 0, nq = 0, i, j, k;
    i32 iters = 2000, acc = 0;
    i32 cap_ms, quiet_ms, kill_ms;
    clock_t t0, t1;
#if defined(__WATCOMC__) && defined(__I86__)
    i16 (__near *history)[6][64];
#else
    i16 (*history)[6][64];
#endif

    /* Kiwipete: 48 moves, 8 captures -- both scan stages exercised. */
    parse_fen(&pos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    n = gen_moves(&pos, list);
    for (i = 0; i < n; i++) {
        u16 m = list[i];
        if (pos.board[mto(m)] != EMPTY || mfl(m) == MF_EP) caps[nc++] = m;
        else quiets[nq++] = m;
    }
#if defined(__WATCOMC__) && defined(__I86__)
    history = (i16 (__near *)[6][64]) &qhist[pos.side];
#else
    history = &qhist[pos.side];
#endif

    t0 = engine_clock();
    MAME_MARK(0x30);
    for (k = 0; k < iters; k++) {
        i16 best = -1, bscore = -32000;
        for (i = 0; i < nc; i++) {
            u16 mv = caps[i];
            i16 s;
            if (mv == ob_ttm) continue;
            s = MVV_LVA(&pos, mv);
            if (s > bscore) { bscore = s; best = i; }
        }
        acc += best + bscore;
    }
    MAME_MARK(0x31);
    t1 = engine_clock();
    cap_ms = (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

    t0 = engine_clock();
    MAME_MARK(0x32);
    for (k = 0; k < iters; k++) {
        i16 best = -1, bscore = -32000;
        for (i = 0; i < nq; i++) {
            u16 q = quiets[i];
            i16 s;
            if (q == ob_ttm || q == ob_k0 || q == ob_k1) continue;
            s = (*history)[TY(pos.board[mfrom(q)]) - 1][(q >> 6) & 0x3F];
            if (s > bscore) { bscore = s; best = i; }
        }
        acc += best + bscore;
    }
    MAME_MARK(0x33);
    t1 = engine_clock();
    quiet_ms = (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

    t0 = engine_clock();
    MAME_MARK(0x34);
    for (k = 0; k < iters; k++)
        for (i = 0; i < nq; i++) acc += quiet_killer_ok(&pos, quiets[i]);
    MAME_MARK(0x35);
    t1 = engine_clock();
    kill_ms = (i32)(t1 - t0) * 1000 / CLOCKS_PER_SEC;

    /* Frozen-score variants: the one-time scoring pass over the list, the
       cached selection step, and the triangular sort comparison. */
    for (i = 0; i < nq; i++) {
        u16 q = quiets[i];
        ob_score[i] = (*history)[TY(pos.board[mfrom(q)]) - 1][(q >> 6) & 0x3F];
    }

    t0 = engine_clock();
    MAME_MARK(0x36);
    for (k = 0; k < iters; k++) {
        i16 best = -1, bscore = -32000;
        for (i = 0; i < nq; i++) {
            u16 q = quiets[i];
            i16 s;
            if (q == ob_ttm || q == ob_k0 || q == ob_k1) continue;
            s = ob_score[i];
            if (s > bscore) { bscore = s; best = i; }
        }
        acc += best + bscore;
    }
    MAME_MARK(0x37);
    t1 = engine_clock();

    t0 = engine_clock();
    MAME_MARK(0x38);
    for (k = 0; k < iters; k++)
        for (i = 0; i < nq; i++) {
            u16 q = quiets[i];
            acc += (*history)[TY(pos.board[mfrom(q)]) - 1][(q >> 6) & 0x3F];
        }
    MAME_MARK(0x39);
    t1 = engine_clock();

    t0 = engine_clock();
    MAME_MARK(0x3A);
    for (k = 0; k < iters; k++) {
        i16 best = 0;
        for (i = 0; i < nq; i++)
            for (j = i + 1; j < nq; j++) {
                acc++;
                if (ob_score[j] > ob_score[best]) best = j;
            }
    }
    MAME_MARK(0x3B);
    t1 = engine_clock();

    printf("orderbench caps=%d quiets=%d iters=%ld acc=%ld\n",
           nc, nq, (long)iters, (long)acc);
    printf("orderbench raw_ms cap=%ld quiet=%ld killer=%ld\n",
           (long)cap_ms, (long)quiet_ms, (long)kill_ms);
    return 0;
}
#endif /* MAME_MARKERS */

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
    /* Same rule as do_make: no capturing pawn, no en-passant target, so the
       parsed signature matches the one a move sequence produces. The pushed
       pawn sits one rank behind the target and its neighbours are the
       would-be capturers. Guard the rank range before probing the board:
       FEN accepts any square in this field. */
    if (p->ep >= 0x20 && p->ep <= 0x57) {
        i16 pc = p->side ? BP : WP;
        i16 to = p->side ? p->ep + 16 : p->ep - 16;
        if (p->board[to - 1] != pc && p->board[to + 1] != pc) p->ep = -1;
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

#ifdef PERFT_TEST
#include "tests/perft_test.inc"
#endif

#ifdef REP_TEST
int repetition_selftest(void);
#endif
#ifdef SEE_TEST
int see_selftest(void);
int see_microbench(void);
#endif
#ifdef SEARCH_TEST
#include "tests/search_state_test.inc"
#endif
#ifdef GENCAPS_DIFF
/* Differential test for the hand-written capture generator.  At every node of
   a depth-2 walk over the perft suite, compare gen_caps_asm against the C
   reference element by element.  Order is part of the contract: the generated
   order feeds move ordering and therefore the bench tree, so an unordered
   multiset compare would be too weak. */
static i32 gc_bad, gc_checks;

static void gc_check(Pos *p) {
    u16 a[256], b[256];
    i16 na = gen_caps_asm(p, a);
    i16 nb = gen_caps_c(p, b);
    i16 i;

    gc_checks++;
    if (na != nb) {
        if (gc_bad < 8)
            printf("COUNT MISMATCH asm=%d c=%d\n", (int)na, (int)nb);
        gc_bad++;
        return;
    }
    for (i = 0; i < na; i++) {
        if (a[i] != b[i]) {
            if (gc_bad < 8)
                printf("MISMATCH i=%d asm=%04X c=%04X\n",
                       (int)i, (unsigned)a[i], (unsigned)b[i]);
            gc_bad++;
        }
    }
}

static void gc_walk(Pos *p, i16 depth) {
    i16 i, n;
    u16 *list;

    gc_check(p);
    if (depth <= 0) return;
    list = movebuf[depth];
    n = gen_moves(p, list);
    for (i = 0; i < n; i++) {
        Undo u;
        i16 us;
        do_make(p, list[i], &u);
        us = p->side ^ 1;
        if (!is_attacked(p, p->ks[us], p->side))
            gc_walk(p, depth - 1);
        undo_move(p, list[i], &u);
    }
}

int gencaps_selftest(void) {
    i16 i;
    gc_bad = 0;
    gc_checks = 0;
    for (i = 0; i < (i16)(sizeof fens / sizeof fens[0]); i++) {
        Pos p;
        parse_fen(&p, fens[i]);
        gc_walk(&p, 2);
    }
    /* Diagnostic FENs: no pieces, no king, en passant for both colours, and
       the promotion paths (push, capture, and a pawn on the far rank). */
    {
        static const char *odd[] = {
            "8/8/8/8/8/8/8/8 w - - 0 1",
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            "8/8/8/3k4/8/8/8/4K3 b - - 0 1",
            "8/8/8/8/3Q4/8/8/4K3 w - - 0 1",
            "P3k3/8/8/8/8/8/8/4K2p w - e6 0 1",
            "4k3/8/8/8/8/8/8/P6p w - h3 0 1",
            "8/8/8/3pP3/8/8/8/4K2k w - d6 0 1",
            "8/8/8/8/8/8/n1n5/1N4Kk w - - 0 1",
            "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
            "r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
            "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
            "4k3/8/8/8/8/8/1p6/4K3 b - - 0 1",
            "4k3/8/8/8/8/8/1p6/r3K3 b - - 0 1",
            "4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1",
            "4k3/8/8/1Pp5/8/8/8/4K3 w - c6 0 1"
        };
        for (i = 0; i < (i16)(sizeof odd / sizeof odd[0]); i++) {
            Pos p;
            parse_fen(&p, odd[i]);
            gc_walk(&p, 1);
        }
    }
    printf("gencaps checks=%ld mismatches=%ld %s\n",
           (long)gc_checks, (long)gc_bad, gc_bad ? "*** FAIL ***" : "ok");
    return gc_bad != 0;
}
#endif

#ifdef ATK_DIFF
/* Differential test for the hand-written attack scan.  Walks the perft suite
   and, at every node, compares is_attacked_asm against the C reference on all
   128 squares for both sides.  is_attacked is a pure predicate, so this is an
   exhaustive equivalence check over the squares that occur in real play. */
static i32 atk_bad, atk_checks;
static void atk_walk(Pos *p, i16 depth) {
    i16 sq, by, i, n;
    u16 *list;

    for (sq = 0; sq < 128; sq++) {
        if (sq & 0x88) continue;
        for (by = 0; by < 2; by++) {
            i16 a = is_attacked_asm(p, sq, by);
            i16 b = is_attacked_c(p, sq, by);
            atk_checks++;
            if (a != b) {
                if (atk_bad < 8)
                    printf("MISMATCH sq=%02X by=%d asm=%d c=%d\n", sq, by, a, b);
                atk_bad++;
            }
        }
    }
    if (depth <= 0) return;
    list = movebuf[depth];
    n = gen_moves(p, list);
    for (i = 0; i < n; i++) {
        Undo u;
        i16 us;
        do_make(p, list[i], &u);
        us = p->side ^ 1;
        if (!is_attacked(p, p->ks[us], p->side))      /* legality, asm path */
            atk_walk(p, depth - 1);
        undo_move(p, list[i], &u);
    }
}

int attack_selftest(void) {
    i16 i;
    atk_bad = 0;
    atk_checks = 0;
    for (i = 0; i < (i16)(sizeof fens / sizeof fens[0]); i++) {
        Pos p;
        parse_fen(&p, fens[i]);
        atk_walk(&p, 2);
    }
    /* Diagnostic FENs can leave ks[] = -1; the reference skips the king probe
       then, and the asm must not read board[-1]. */
    {
        static const char *odd[] = {
            "8/8/8/8/8/8/8/8 w - - 0 1",
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            "8/8/8/3k4/8/8/8/4K3 b - - 0 1",
            "8/8/8/8/3Q4/8/8/4K3 w - - 0 1"
        };
        for (i = 0; i < (i16)(sizeof odd / sizeof odd[0]); i++) {
            Pos p;
            parse_fen(&p, odd[i]);
            atk_walk(&p, 0);
        }
    }
    printf("atkdiff checks=%ld mismatches=%ld %s\n",
           (long)atk_checks, (long)atk_bad, atk_bad ? "*** FAIL ***" : "ok");
    return atk_bad != 0;
}
#endif
int main(int argc, char **argv) {
    i16 test = 0;
    i16 j, nn_log = 1;
    Pos pos;

    /* Metadata export must work without loading a network or printing a banner. */
    if (argc > 1 && strcmp(argv[1], "spsa") == 0) {
#ifdef TUNE
        search_tune_spsa();
        return 0;
#else
        fprintf(stderr, "SPSA options disabled; rebuild with TUNE=1 or -Tune.\n");
        return 1;
#endif
    }

    mvv_build();   /* one-time MVV-LVA table (dedicated init) */
    king_line_build(); /* one-time legality lookup (dedicated init) */
    cast_mask_build(); /* one-time castling-rights table (dedicated init) */
    zob_init();    /* one-time Zobrist key tables (dedicated init) */
    lmr_build();   /* one-time LMR log table (dedicated init) */
    timing_init(); /* one-time wall clock (8253/8254 ch0 mode 2 on DOS) */
#ifdef ASM_GENCAPS
    move_tables_build(); /* one-time move-encoding tables for gen_opt.asm */
#endif

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

#ifdef REP_TEST
    if (argc > 1 && strcmp(argv[1], "reptest") == 0) return repetition_selftest();
#endif
#ifdef SEARCH_TEST
    if (argc > 1 && strcmp(argv[1], "searchtest") == 0) return search_state_selftest();
#endif
#ifdef TIME_TEST
    if (argc > 1 && strcmp(argv[1], "timetest") == 0) return time_selftest();
#endif
#ifdef SEE_TEST
    if (argc > 1 && strcmp(argv[1], "seetest") == 0) return see_selftest();
    if (argc > 1 && strcmp(argv[1], "seebench") == 0) return see_microbench();
#endif
#ifdef PERFT_TEST
    if (argc > 1 && strcmp(argv[1], "perft") == 0)
        return perft_selftest(argc - 2, argv + 2);
#endif
#ifdef NN_TEST
    if (argc > 1 && strcmp(argv[1], "nn") == 0)
        return nnue_selftest((argc > 2) ? argv[2] : NULL);
#endif

#ifdef MAME_MARKERS
    if (argc > 1 && strcmp(argv[1], "orderbench") == 0)
        return order_bench();
#endif

#ifdef GENCAPS_DIFF
    if (argc > 1 && strcmp(argv[1], "gencaps") == 0)
        return gencaps_selftest();
#endif
#ifdef ATK_DIFF
    if (argc > 1 && strcmp(argv[1], "atkdiff") == 0)
        return attack_selftest();
#endif
    if (argc > 1 && strcmp(argv[1], "memprobe") == 0)
        return mem_probe();

    if (argc > 1 && strcmp(argv[1], "fbench") == 0)
        return nnue_fwd_bench();

    if (argc > 1 && strcmp(argv[1], "nbench") == 0)
        return nnue_bench();

    if (argc > 1 && strcmp(argv[1], "sbench") == 0)
        return sbench();

#ifdef MAME_MARKERS
    if (argc > 1 && strcmp(argv[1], "ttbench") == 0)
        return tt_bench();
#endif

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "xboard") == 0))
        return xboard_main();

    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        int bd = 0;                     /* 0 = BENCH_DEPTH default */
        if (argc > 2) bd = atoi(argv[2]);
        return bench(bd);
    }

#ifdef VCLOCK
    if (argc > 1 && strcmp(argv[1], "calcheck") == 0)
        return calcheck();
#endif

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

    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}

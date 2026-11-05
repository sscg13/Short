/* attack_ref.c - C reference for the hand-written is_attacked
   (build.ps1 -AsmAttack; never part of a normal build)

   is_attacked_c()   the original C body, verbatim, for the differential test
   is_attacked_rook_c() / is_attacked_bishop_c()  the two ray halves, kept
                     separate so the asm could adopt them one piece type at a
                     time during bring-up; is_attacked_lines_c() joins them.

   All of it must stay bit-identical to the body in chess.c (the `#else` branch
   of is_attacked).  The `atkdiff` selftest compares the asm against the
   reference on every (square, side) pair of every node it walks, so a
   divergence is caught immediately rather than at bench time. */

#include "engine.h"

/* chess.c keeps these file-static; duplicate the four values here. */
static const i16 rb[4] = { -16, 1, 16, -1 };
static const i16 bb[4] = { -17, -15, 15, 17 };

/* ------------------------------------------------------------------ */
/* ray halves, split so the asm can adopt them one piece type at a time */
/* ------------------------------------------------------------------ */
i16 is_attacked_rook_c(Pos *p, i16 sq, i16 by) {
    i16 i, to, d;
    i16 pc;

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
    return 0;
}

i16 is_attacked_bishop_c(Pos *p, i16 sq, i16 by) {
    i16 i, to, d;
    i16 pc;

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

i16 is_attacked_lines_c(Pos *p, i16 sq, i16 by) {
    if (is_attacked_rook_c(p, sq, by)) return 1;
    return is_attacked_bishop_c(p, sq, by);
}

/* ------------------------------------------------------------------ */
/* full reference: the original body, unchanged                        */
/* ------------------------------------------------------------------ */
i16 is_attacked_c(Pos *p, i16 sq, i16 by) {
    i16 i, to, d;
    i16 pc;

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

    return is_attacked_lines_c(p, sq, by);
}

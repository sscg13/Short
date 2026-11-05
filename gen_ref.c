/* gen_ref.c - C reference for the hand-written capture generator
   (build.ps1 -GenCapsDiff; never part of a normal build)

   gen_caps_c() is the original C body, verbatim, for the differential test.
   It must stay bit-identical to the body in chess.c (the `#else` branch of
   gen_caps).  The `gencaps` selftest compares the asm against it move by move
   -- order included, because the generated order feeds move ordering and
   therefore the bench tree -- at every node it walks. */

#include "engine.h"

/* chess.c keeps these file-static; duplicate them here. */
static const i16 kn[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const i16 ki[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 rb[4] = { -16, 1, 16, -1 };
static const i16 bb[4] = { -17, -15, 15, 17 };
static const i16 qd[8] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const i16 pw[4] = { WN, WB, WR, WQ };
static const i16 pb[4] = { BN, BB, BR, BQ };

i16 gen_caps_c(Pos *p, u16 *list) {
    i16 n = 0, from, to, pc, pt, us = p->side, them = us ^ 1;
    i16 i, d, fwd, r;

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

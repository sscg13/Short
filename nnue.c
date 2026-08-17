/* nnue.c - small NNUE evaluation: 704-feature incremental accumulators.
 *
 * Architecture (see NNUE.md):
 *   704 one-hot inputs -> two N-wide i16 accumulators (white POV, black POV)
 *   sharing ONE weight matrix, plus a shared N-wide layer-1 bias ->
 *   symmetric clamp clamp(pre,-1,1) at accumulator quantization 128 (the
 *   +/-128 extremes are shift-only: 128*w = w<<7) -> 2N i8 output weights
 *   (x64) -> i16 output bias (x8192) -> raw score (>> NNUE_SCALE_SHIFT = cp).
 *
 * The black POV is the white POV of the position rotated 180 degrees with
 * colors swapped, so the net is color-symmetric by construction. Both POVs
 * additionally HORIZONTALLY MIRROR every square when their own king is on the
 * e-h files, so the perspective king always lands in files a-d. Because a
 * position and its left-right mirror get the same features, the net is also
 * left-right symmetric by construction (and the 32 king buckets fold e-h ->
 * a-d without losing information).
 *
 * Incremental: do_make() applies per-feature-row deltas to both accumulators.
 * PLY-INDEXED accumulator: nn_acc[ply][2][NNUE_N] is a stack of snapshots
 * indexed by the search ply (33 x 256 B, the same footprint the copy-make
 * snapshot stack used). nnue_make writes the child's accumulator FRESH into
 * slot nn_ply+1 (dst[j] = src[j] + delta[j], or a full recompute for a mirror
 * flip) and increments nn_ply; nnue_undo just decrements nn_ply - NO
 * snapshot/restore memcpy is kept, so a make/undo pair costs one 64-element
 * pass instead of copy-make's save+apply+restore. nnue_eval reads slot nn_ply.
 * The big weight matrix lives in a far segment on 16-bit. A king move that
 * crosses the d/e file boundary flips a POV's mirror flag (every piece
 * re-indexes), so that POV is recomputed from scratch (into the new slot)
 * instead of applying deltas.
 *
 * Feature layout (704 rows, one perspective, in king-normalized coordinates).
 * "own" = the perspective side, "enemy" = the other side. Squares are compact
 * 0..63 (rank = sq>>3, file = sq&7):
 *
 *   rows 0..31     own king        32 buckets (files a-d after the mirror)
 *   rows 32..79    own pawns       48 squares (real ranks 2-7 = index 1..6)
 *   rows 80..335   own N,B,R,Q     4 x 64
 *   rows 336..399  enemy king      64
 *   rows 400..447  enemy pawns     48
 *   rows 448..703  enemy N,B,R,Q   4 x 64
 */

#include "engine.h"

i16 nnue_enabled = 0;
i16 nnue_active = 0;

#if defined(PROFILE) || defined(VCLOCK)
i32 c_nn_make = 0;              /* nnue_make entries */
i32 c_nn_undo = 0;              /* nnue_undo entries */
i32 c_nn_eval = 0;              /* nnue_eval entries */
i32 c_refresh = 0;              /* feature-row deltas applied (nn_delta_apply) */
i32 c_flip = 0;                 /* mirror-flip recompute paths (nnue_make) */
#endif

#if defined(__WATCOMC__) && !defined(__386__)
/* 45 KB won't fit the ~23 KB of free near data; put it in a far data segment. */
i8 _far nn_w1[NNUE_W1_SIZE];
#else
i8 nn_w1[NNUE_W1_SIZE];
#endif
i8 nn_b1[NNUE_N];   /* layer-1 bias (per hidden neuron, shared by both POVs) */
i8 nn_w2[NNUE_W2_SIZE];
i16 nn_bias;        /* output bias, i16, quantized at 128*64 (WORD for the asm fwd) */

#if !defined(__WATCOMC__)
/* Embed the net into the binary (gcc build: OpenBench runs the bare binary, so
   there is no runtime file dependency). The path defaults to chess.net and is
   overridable at build time: OpenBench passes EVALFILE=<net> to make, which
   defines NN_EMBED_FILE=<path>; NN_STR stringifies it for the .incbin directive.
   Rebuild after re-converting a net. */
#ifndef NN_EMBED_FILE
#define NN_EMBED_FILE chess.net
#endif
#define NN_STR_(x) #x
#define NN_STR(x) NN_STR_(x)
__asm__(
    ".section .rodata\n"
    ".global nn_embedded_net_start\n"
    "nn_embedded_net_start:\n"
    ".incbin \"" NN_STR(NN_EMBED_FILE) "\"\n"
    ".global nn_embedded_net_end\n"
    "nn_embedded_net_end:\n"
    ".text\n"
);
extern const u8 nn_embedded_net_start[];
extern const u8 nn_embedded_net_end[];
#endif

i16 nn_acc[MAXPLY + 1][2][NNUE_N];  /* ply-indexed pre-activation stack:
                                       [ply][POV][j]; nnue_make writes slot
                                       nn_ply+1, nnue_undo decrements nn_ply
                                       (non-static so nnue_opt.asm can ref it) */
i16 nn_ply = 0;          /* current ply: index of the active accumulator slot,
                            kept in sync with the board's make/undo depth */

#if defined(__WATCOMC__) && !defined(__386__)
/* hand-unrolled 64-element apply loops (nnue_opt.asm) for the 16-bit build:
   acc[persp][j] += (short)w1[row*64+j]  /  -=  for j in 0..63 */
void nn_apply_add(i16 persp, i16 row);
void nn_apply_sub(i16 persp, i16 row);
#define NNUE_ASM_APPLY 1

/* batched ply-indexed make loops (nnue_opt.asm): one 64-element copy+apply pass
   writing slot nn_ply+1. nn_make_move: dst = src + w1[to] - w1[from].
   nn_make_cap:  dst = src + w1[to] - w1[from] - w1[cap]. The src (slot nn_ply)
   is read via the _nn_ply global; dst is always src + 256 bytes. */
void nn_make_move(i16 persp, i16 to_row, i16 from_row);
void nn_make_cap(i16 persp, i16 to_row, i16 from_row, i16 cap_row);
#define NNUE_ASM_BATCH 1

/* per-slot forward product tables (nnue_opt.asm, NNUE_OPTIMIZATION.md §5):
   fwd[p][j][a] = (a^2 * w2[p*64+j]) >> NNUE_ACT2_SHIFT  for a in [0,255]
   (the ReLU^2 activation, pre-shifted so every entry fits i16). Built at net
   load so the forward multiply becomes one word load. ONE 64 KB far array
   holding both perspectives so the linker CANNOT reorder them: the asm fwd
   reads fwd[0] at offset 0 and fwd[1] at +32768 of the same segment (a single
   2D declaration makes that layout guaranteed). */
i16 _far nn_fwd[2][NNUE_N][256];
Score nn_fwd_eval(i16 side);          /* generated asm forward pass (ReLU^2) */
#ifndef NNUE_DISABLE_ASM_FWD
#define NNUE_ASM_FWD 1
#endif
#endif

/* ------------------------------------------------------------------ */
/* feature indexing                                                   */
/* ------------------------------------------------------------------ */

/* per-perspective mirror flags from the king squares: mirror a POV's board
   when that POV's king (after its 180 transform for persp 1) sits on e-h,
   so the own king always normalizes to files a-d. */
static void nn_mirrors(Pos *p, i16 m[2]) {
    m[0] = ((sq2c(p->ks[0]) & 7) >= 4) ? 1 : 0;        /* white king file >= e */
    m[1] = ((7 - (sq2c(p->ks[1]) & 7)) >= 4) ? 1 : 0;  /* R180 of black king file >= e */
}

/* feature row for a color-normalized piece on a mirror-normalized compact
   square (0..703, or -1 = invalid, e.g. a pawn on rank 1/8). This is the
   pure dispatch used to build the nn_row table (and as the gcc scalar oracle
   before the table is built). */
static i16 nn_row_dispatch(i16 pc, i16 c) {
    i16 ty = TY(pc);
    if (CO(pc) == 0) {                /* own piece */
        if (ty == 6) return (c >> 3) * 8 + (c & 7);   /* own king: file <= 3 by mirror */
        if (ty == 1) { i16 r = c >> 3; if (r < 1 || r > 6) return -1; return 32 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 80 + (ty - 2) * 64 + c;
        return -1;
    } else {                          /* enemy piece */
        if (ty == 6) return 336 + c;
        if (ty == 1) { i16 r = c >> 3; if (r < 1 || r > 6) return -1; return 400 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 448 + (ty - 2) * 64 + c;
        return -1;
    }
}

/* nn_row lookup table: [pc][normalized compact square] -> row or -1. Built
   once from nn_row_dispatch; replaces the branchy per-call dispatch (nn_row is
   called once per feature apply, 114,921x in profile-1). 15x64 i16 = 1.9 KB
   near data (DGROUP has ~10 KB headroom). */
static i16 nn_rowtab[15][64];
static i16 nn_rowtab_ready;

static void nn_rowtab_build(void) {
    i16 pc, c;
    for (pc = 1; pc < 15; pc++)
        for (c = 0; c < 64; c++)
            nn_rowtab[pc][c] = nn_row_dispatch(pc, c);
    nn_rowtab_ready = 1;
}

/* feature row for a piece at sq88 in this POV, given the mirror flag */
static i16 nn_row(i16 persp, i16 pc, i16 sq88, i16 mirror) {
    i16 c = sq2c(sq88);
    if (persp == 1) {                 /* black POV: 180 flip + color swap */
        pc = CO(pc) ? pc - 8 : pc + 8;
        c = 63 - c;
    }
    if (mirror) c = (c & 0xF8) | (7 - (c & 7));
    return nn_rowtab[pc][c];
}

/* Batched ply-indexed feature-row apply (make path). nn_batch_addsub writes
   dst[j] = src[j] + w1[to][j] - w1[from][j] in ONE pass (src = slot nn_ply,
   dst = slot nn_ply+1; the asm path or the scalar C oracle), and nn_batch_cap
   writes dst[j] = src[j] + w1[to][j] - w1[from][j] - w1[cap][j]. Bit-identical
   to the old in-place RMW (i16 arithmetic mod 2^16), so node counts are
   unchanged by construction. A negative row means the piece has no feature
   (e.g. a pawn on the promotion rank) - the delta is zero, but the child slot
   STILL gets written, so the perspective is plain-copied to it. */
static void nn_batch_addsub(i16 persp, i16 to_row, i16 from_row) {
    i16 *dst = nn_acc[nn_ply + 1][persp];
    const i16 *src = nn_acc[nn_ply][persp];
    if (to_row < 0 || from_row < 0) {
        i16 j;
        for (j = 0; j < NNUE_N; j++) dst[j] = src[j];
        return;
    }
    PCOUNT(c_refresh);
#ifdef NNUE_ASM_BATCH
    nn_make_move(persp, to_row, from_row);
#else
    {
        u16 b0 = (u16)to_row << 6, b1 = (u16)from_row << 6;
        i16 j;
        for (j = 0; j < NNUE_N; j++)
            dst[j] = (i16)(src[j] + (i16)nn_w1[b0 + j] - (i16)nn_w1[b1 + j]);
    }
#endif
}

static void nn_batch_cap(i16 persp, i16 to_row, i16 from_row, i16 cap_row) {
    i16 *dst = nn_acc[nn_ply + 1][persp];
    const i16 *src = nn_acc[nn_ply][persp];
    if (to_row < 0 || from_row < 0 || cap_row < 0) {
        i16 j;
        for (j = 0; j < NNUE_N; j++) dst[j] = src[j];
        return;
    }
    PCOUNT(c_refresh);
#ifdef NNUE_ASM_BATCH
    nn_make_cap(persp, to_row, from_row, cap_row);
#else
    {
        u16 b0 = (u16)to_row << 6, b1 = (u16)from_row << 6;
        u16 b2 = (u16)cap_row << 6;
        i16 j;
        for (j = 0; j < NNUE_N; j++)
            dst[j] = (i16)(src[j] + (i16)nn_w1[b0 + j] - (i16)nn_w1[b1 + j]
                          - (i16)nn_w1[b2 + j]);
    }
#endif
}

/* compute one perspective's accumulator from scratch (init with the layer-1 bias) */
static void nn_compute_persp(Pos *p, i16 persp, i16 *out) {
    i16 m[2], sq, k;
    nn_mirrors(p, m);
    for (k = 0; k < NNUE_N; k++) out[k] = nn_b1[k];
    for (sq = 0; sq < 128; sq++) {
        i16 pc = p->board[sq];
        i16 row;
        u16 base;
        if (!pc) continue;
        row = nn_row(persp, pc, sq, m[persp]);
        if (row < 0) continue;
        base = (u16)row << 6;
        for (k = 0; k < NNUE_N; k++)
            out[k] += (i16)nn_w1[base + k];
    }
}

/* compute both accumulators from scratch (root of a search) */
static void nn_compute(Pos *p, i16 out[2][NNUE_N]) {
    nn_compute_persp(p, 0, out[0]);
    nn_compute_persp(p, 1, out[1]);
}

void nnue_reset(Pos *p) {
    nn_compute(p, nn_acc[0]);       /* full recompute into slot 0 */
    nn_ply = 0;
}

/* ------------------------------------------------------------------ */
/* incremental make / undo                                            */
/* ------------------------------------------------------------------ */

/* Precomputed castling deltas. A castle is a fixed set of piece-square
   changes, so the accumulator change can be precomputed once (per net load)
   for the cases that keep a perspective's mirror flag unchanged:
     case 0 = kingside, castling side's own perspective,
     case 1 = kingside, the OTHER (nstm) perspective,
     case 2 = queenside, the OTHER (nstm) perspective  (the castling side's
              own perspective always flips on queenside: the king crosses the
              d/e boundary, so it is recomputed from scratch instead).
   Rows are computed with the ACTUAL perspective that will use them (own vs
   nstm) and both mirror values, so the runtime lookup is an exact index.
   Index [case][mover_col][mirror][j]. 3*2*2*64 i16 = 1.5 KB near data.
   Built at net load. */
static i16 cast_delta[3][2][2][NNUE_N];

static void nn_castle_build(void) {
    /* (king from, king to, rook from, rook to) as 0x88 squares, per color.
       The perspective that uses each case is: case 0 = the castling side's
       OWN perspective; cases 1-2 = the OTHER (nstm) perspective. Each delta
       is built with the actual persp, pieces and squares of that color. */
    static const i16 ks_sq[2][2][4] = {
        /* O-O */
        { { 0x04, 0x06, 0x07, 0x05 },   /* white: e1->g1, h1->f1 */
          { 0x74, 0x76, 0x77, 0x75 } }, /* black: e8->g8, h8->f8 */
        /* O-O-O */
        { { 0x04, 0x02, 0x00, 0x03 },   /* white: e1->c1, a1->d1 */
          { 0x74, 0x72, 0x70, 0x73 } }, /* black: e8->c8, a8->d8 */
    };
    static const i16 case_cast[3] = { 0, 0, 1 };   /* 0=O-O, 1=O-O-O */
    i16 c, w, m, j;

    for (c = 0; c < 3; c++) {
        for (w = 0; w < 2; w++) {
            const i16 *s = ks_sq[case_cast[c]][w];
            i16 persp = (c == 0) ? w : (w ^ 1);   /* case 0: own persp; 1-2: nstm */
            i16 king = (w == 0) ? WK : BK;
            i16 rook = (w == 0) ? WR : BR;
            for (m = 0; m < 2; m++) {
                i16 kf = nn_row(persp, king, s[0], m);
                i16 kt = nn_row(persp, king, s[1], m);
                i16 rf = nn_row(persp, rook, s[2], m);
                i16 rt = nn_row(persp, rook, s[3], m);
                for (j = 0; j < NNUE_N; j++) {
                    i16 a = 0;
                    if (kt >= 0) a += (i16)nn_w1[((u16)kt << 6) + j];
                    if (kf >= 0) a -= (i16)nn_w1[((u16)kf << 6) + j];
                    if (rt >= 0) a += (i16)nn_w1[((u16)rt << 6) + j];
                    if (rf >= 0) a -= (i16)nn_w1[((u16)rf << 6) + j];
                    cast_delta[c][w][m][j] = a;
                }
            }
        }
    }
}

/* apply the precomputed castle delta to one perspective (mirror unchanged):
   copy+apply into the child slot (dst[j] = src[j] + delta[j]) */
static void nn_castle_apply(i16 persp, i16 case_idx, i16 mover_col, i16 mirror) {
    i16 j;
    const i16 *d = cast_delta[case_idx][mover_col][mirror];
    i16 *dst = nn_acc[nn_ply + 1][persp];
    const i16 *src = nn_acc[nn_ply][persp];
    for (j = 0; j < NNUE_N; j++)
        dst[j] = (i16)(src[j] + d[j]);
}

void nnue_make(Pos *p, u16 m, Undo *u) {
    i16 from = mfrom(m), to = mto(m), fl = mfl(m);
    i16 mover_col = p->side ^ 1;          /* side that just moved */
    i16 mover = p->board[to];             /* mover or promo piece at 'to' */
    i16 newp = mover;
    i16 persp, mpost[2], mpre[2], flip[2];

    PCOUNT(c_nn_make);
    if (ispromo(m)) mover = (mover_col == 0) ? WP : BP;   /* it was a pawn */

    /* mirror flags before/after this move (only a king move can change them).
       Every perspective's delta (or full recompute) is written to the CHILD
       slot nn_ply+1; the current slot stays intact as the undo restore point. */
    nn_mirrors(p, mpost);
    mpre[0] = (mover == WK) ? ((sq2c(from) & 7) >= 4) : mpost[0];
    mpre[1] = (mover == BK) ? ((7 - (sq2c(from) & 7)) >= 4) : mpost[1];
    for (persp = 0; persp < 2; persp++)
        flip[persp] = (mpre[persp] != mpost[persp]) ? 1 : 0;

    for (persp = 0; persp < 2; persp++) {
        if (flip[persp]) {
            PCOUNT(c_flip);
        } else if (fl == MF_CASTLE) {
            /* castle with no mirror change: precomputed delta.
               kingside: both perspectives (own case 0, other case 1).
               queenside: only the other (nstm) perspective reaches here (the
               castling side's own perspective flipped and recomputes below). */
            i16 case_idx;
            if (to == 0x06 || to == 0x76)      /* kingside */
                case_idx = (persp == mover_col) ? 0 : 1;
            else                                 /* queenside */
                case_idx = 2;
            PCOUNT(c_refresh);
            nn_castle_apply(persp, case_idx, mover_col, mpost[persp]);
        } else {
            i16 to_row, from_row, cap_row = -1;
            to_row   = nn_row(persp, newp, to, mpost[persp]);
            from_row = nn_row(persp, mover, from, mpost[persp]);
            if (fl == MF_EP) {
                i16 esq = (mover_col == 0) ? to - 16 : to + 16;
                i16 ep = (mover_col == 0) ? BP : WP;
                cap_row = nn_row(persp, ep, esq, mpost[persp]);
            } else if (u->cap != EMPTY) {
                cap_row = nn_row(persp, u->cap, to, mpost[persp]);
            }
            if (cap_row >= 0)
                nn_batch_cap(persp, to_row, from_row, cap_row);
            else
                nn_batch_addsub(persp, to_row, from_row);
        }
    }
    for (persp = 0; persp < 2; persp++)
        if (flip[persp]) nn_compute_persp(p, persp, nn_acc[nn_ply + 1][persp]);
    nn_ply++;                     /* child slot is now the active accumulator */
}

void nnue_undo(Pos *p) {
    PCOUNT(c_nn_undo);
    (void)p;
    nn_ply--;                     /* the parent's slot is untouched */
}

/* ------------------------------------------------------------------ */
/* forward pass                                                       */
/* ------------------------------------------------------------------ */

Score nnue_eval(Pos *p) {
    PCOUNT(c_nn_eval);
#ifdef NNUE_ASM_FWD
    return nn_fwd_eval(p->side);
#else
    {
        i32 out = nn_bias;
        i16 j;
        /* ReLU^2: act = clamp(acc, 0, 255); term = (act^2 * w2) >> 8. The
           squared pre-activation (max 255^2*127 ~ 8.26M) is pre-shifted so the
           forward table still fits i16; out stays i32 and the final >>5
           (1.0 = 256 cp) is unchanged. stm/nstm: acc[0] is the white POV,
           acc[1] the black POV. The net is side-to-move-aware (the trainer
           always treats the side to move as "white" in feature space), so when
           black is to move the weight roles SWAP (acc[1] is the stm POV) and
           there is NO final negate - the score is already from the side to
           move's perspective. */
        for (j = 0; j < NNUE_N; j++) {
            i16 a0 = nn_acc[nn_ply][0][j], a1 = nn_acc[nn_ply][1][j];
            i16 ws = nn_w2[j];                  /* stm weight */
            i16 wn = nn_w2[NNUE_N + j];         /* nstm weight */
            i16 as = (p->side == 0) ? a0 : a1;  /* stm activation */
            i16 an = (p->side == 0) ? a1 : a0;  /* nstm activation */
            i32 xs = (as < 0) ? 0 : (as > 255 ? 255 : as);
            i32 xn = (an < 0) ? 0 : (an > 255 ? 255 : an);
            out += (xs * xs * ws) >> NNUE_ACT2_SHIFT;
            out += (xn * xn * wn) >> NNUE_ACT2_SHIFT;
        }
        return (Score)(out >> NNUE_SCALE_SHIFT);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* weight loading                                                      */
/* ------------------------------------------------------------------ */

#ifdef NNUE_ASM_FWD
/* fill fwd[p][j][a] = (a^2 * w2[p*64+j]) >> NNUE_ACT2_SHIFT for a = 0..255 from
   the current nn_w2 (index = act in [0,255]). The squared product is
   pre-shifted so every entry fits i16 (max 255^2*127>>9 = 16129). Entries are
   32k far word stores, one-time. */
static void nn_fwd_build(void) {
    i16 j, a;
    for (j = 0; j < NNUE_N; j++) {
        i16 w0 = nn_w2[j];
        i16 w1 = nn_w2[NNUE_N + j];
        for (a = 0; a < 256; a++) {
            i32 p0 = (i32)a * a * w0;
            i32 p1 = (i32)a * a * w1;
            nn_fwd[0][j][a] = (i16)(p0 >> NNUE_ACT2_SHIFT);
            nn_fwd[1][j][a] = (i16)(p1 >> NNUE_ACT2_SHIFT);
        }
    }
}
#endif

/* parse a net blob (engine format, see NNUE.md) from memory */
static int nnue_parse_blob(const u8 *p, i32 len) {
    u16 ver, feats, hN;
    i32 w1sz = (i32)NNUE_FEATURES * NNUE_N;
    i32 need = 12 + w1sz + NNUE_N + NNUE_W2_SIZE + 2;
    if (len < need) return 0;
    if (p[0] != 'N' || p[1] != 'N' || p[2] != 'U' || p[3] != 'E') return 0;
    ver   = (u16)p[4] | ((u16)p[5] << 8);
    feats = (u16)p[6] | ((u16)p[7] << 8);
    hN    = (u16)p[8] | ((u16)p[9] << 8);
    if (ver != 2) return 0;                        /* v2 ReLU^2 is the only format */
    if (feats != NNUE_FEATURES || hN != NNUE_N) return 0;
    memcpy(nn_w1, p + 12, (size_t)w1sz);
    memcpy(nn_b1, p + 12 + w1sz, (size_t)NNUE_N);
    memcpy(nn_w2, p + 12 + w1sz + NNUE_N, (size_t)NNUE_W2_SIZE);
    nn_bias = (i16)((u16)(u8)p[12 + w1sz + NNUE_N + NNUE_W2_SIZE]
            | ((u16)(u8)p[13 + w1sz + NNUE_N + NNUE_W2_SIZE] << 8));
    nnue_enabled = 1;
    nnue_tables_init();
    return 1;
}

/* ------------------------------------------------------------------ */
/* one-time table builds (dedicated init: all precomputation/setup     */
/* ------------------------------------------------------------------ */

/* build every net-dependent table: the feature-row lookup, the forward
   product tables, and the castling deltas. Must run after the weights are in
   memory (nn_w1/nn_w2). Call once at net load; idempotent via the ready flag. */
void nnue_tables_init(void) {
    nn_rowtab_build();
#ifdef NNUE_ASM_FWD
    nn_fwd_build();
#endif
    nn_castle_build();
}

/* load the net file directly into the static far arrays, STREAMING the sections
   so NO temporary buffer is malloc'd. The 45 KB blob temp used to need a heap
   block that a tight-memory 16-bit machine (e.g. the 80286 emulator @ 512 KB,
   ~40 KB free) cannot spare. Same bytes land in the same arrays as
   nnue_parse_blob, so the result is bit-identical. */
int nnue_load(const char *path) {
    FILE *f = fopen(path, "rb");
    u8 hdr[12];
    u16 ver, feats, hN;
    i32 w1sz = (i32)NNUE_FEATURES * NNUE_N;
    if (!f) return 0;
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return 0; }
    if (hdr[0] != 'N' || hdr[1] != 'N' || hdr[2] != 'U' || hdr[3] != 'E') { fclose(f); return 0; }
    ver   = (u16)hdr[4] | ((u16)hdr[5] << 8);
    feats = (u16)hdr[6] | ((u16)hdr[7] << 8);
    hN    = (u16)hdr[8] | ((u16)hdr[9] << 8);
    if (ver != 2) { fclose(f); return 0; }       /* v2 ReLU^2 is the only format */
    if (feats != NNUE_FEATURES || hN != NNUE_N) { fclose(f); return 0; }
    if (fread(nn_w1, 1, (size_t)w1sz, f) != (size_t)w1sz) { fclose(f); return 0; }
    if (fread(nn_b1, 1, (size_t)NNUE_N, f) != NNUE_N) { fclose(f); return 0; }
    if (fread(nn_w2, 1, (size_t)NNUE_W2_SIZE, f) != NNUE_W2_SIZE) { fclose(f); return 0; }
    if (fread(hdr, 1, 2, f) != 2) { fclose(f); return 0; }
    nn_bias = (i16)((u16)(u8)hdr[0] | ((u16)(u8)hdr[1] << 8));
    fclose(f);
    nnue_enabled = 1;
    nnue_tables_init();
    return 1;
}

/* try the file first (lets --nnue override the bundled net), then the
   embedded copy on the gcc build (OpenBench runs the bare binary) */
#ifdef NO_NNUE
/* measurement build: net stays disabled (material eval, no accumulator work) so
   the profile total is pure search + movegen + make/undo */
int nnue_ensure_loaded(const char *path) { (void)path; return 0; }
#else
int nnue_ensure_loaded(const char *path) {
    if (nnue_enabled) return 1;
#if defined(__WATCOMC__)
    /* 16-bit build: no embedded net; the default net is the chess.net file. */
    return nnue_load(path);
#else
    /* gcc build: the EMBEDDED net is the default (OpenBench embeds the trained
       net via EVALFILE). The file is only used via --nnue (main calls nnue_load
       directly), so a stale chess.net in the CWD cannot shadow the embedded net
       and the default is always NNUE. */
    (void)path;
    return nnue_parse_blob(nn_embedded_net_start,
                           (i32)(nn_embedded_net_end - nn_embedded_net_start));
#endif
}
#endif

/* ensure the default net is loaded; idempotent, so it is safe to call at every
   search entry point (bench/think/search_root/profile) to make NNUE the default.
   The gcc default is the embedded blob (Makefile's EVALFILE default, currently
   chess-v2-finetune.net - the path below is ignored there). The 16-bit build has
   no embedded net and ships the net as the FAT 8.3 blob CHESS.NET on the emulator
   floppies (chess-v2-finetune.net is not a valid 8.3 name, so it loads chess.net). */
int nnue_ensure_default(void) {
#if defined(__WATCOMC__)
    return nnue_ensure_loaded("chess.net");
#else
    return nnue_ensure_loaded("chess-v2-finetune.net");
#endif
}

/* ------------------------------------------------------------------ */
/* self-test: `chess nn [fen]`                                        */
/* ------------------------------------------------------------------ */

static void flip_pos(Pos *dst, const Pos *src) {
    i16 c;
    memset(dst, 0, sizeof *dst);
    for (c = 0; c < 64; c++) {
        i16 pc = src->board[c2sq(c)];
        if (pc) dst->board[c2sq(63 - c)] = pc ^ 8;   /* rotate 180 + swap colors */
    }
    dst->side = src->side ^ 1;
    dst->castle = 0;
    dst->ep = -1;
    /* re-locate kings: nn_compute reads ks[] for the mirror flags */
    for (c = 0; c < 64; c++) {
        i16 pc = dst->board[c2sq(c)];
        if (pc == WK) dst->ks[0] = c2sq(c);
        else if (pc == BK) dst->ks[1] = c2sq(c);
    }
}

/* horizontal mirror (file flip, colors and side unchanged) */
static void hmirror_pos(Pos *dst, const Pos *src) {
    i16 c;
    memset(dst, 0, sizeof *dst);
    for (c = 0; c < 64; c++) {
        i16 f = c & 7;
        i16 mc = (c & 0xF8) | (7 - f);
        i16 pc = src->board[c2sq(c)];
        if (pc) dst->board[c2sq(mc)] = pc;
    }
    dst->side = src->side;
    dst->castle = 0;
    dst->ep = -1;
    for (c = 0; c < 64; c++) {
        i16 pc = dst->board[c2sq(c)];
        if (pc == WK) dst->ks[0] = c2sq(c);
        else if (pc == BK) dst->ks[1] = c2sq(c);
    }
}

/* direct per-call cost measurement (`chess nbench`): times nnue_eval and a
   make/undo pair in loops, prints ms per 1000 calls. Convert to cycles with the
   host clock (8088 16 MHz: cyc = ms*16 per call; 286 6 MHz: cyc = ms*6). */
int nnue_bench(void) {
    static Pos pos;
    static u16 list[256];
    Undo u;
    i16 i, n, iters;
    clock_t t0, t1;
    i32 eval_ms, delta_ms;

    parse_fen(&pos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    nnue_reset(&pos);
    nnue_active = 1;
    n = gen_moves(&pos, list);
    if (n <= 0) return 1;

    iters = 4000;
    t0 = clock();
    for (i = 0; i < iters; i++) nnue_eval(&pos);
    t1 = clock();
    eval_ms = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;

    iters = 10000;
    {
        u16 qm = 0;
        for (i = 0; i < n; i++)
            if (mfl(list[i]) == 0) { qm = list[i]; break; }
        if (!qm) return 1;
        t0 = clock();
#ifdef PROFILE
        {
            i32 r0 = c_refresh, f0 = c_flip;
#endif
        for (i = 0; i < iters; i++) {
            do_make(&pos, qm, &u);
            undo_move(&pos, qm, &u);
        }
#ifdef PROFILE
            printf("nbench applies=%ld flips=%ld\n", (long)(c_refresh - r0), (long)(c_flip - f0));
        }
#endif
        t1 = clock();
        delta_ms = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
    }

    printf("nbench eval1000=%ld delta1000=%ld\n",
           (long)(eval_ms * 1000 / 4000), (long)(delta_ms * 1000 / 10000));
    nnue_active = 0;
    return 0;
}

int nnue_selftest(const char *fen) {
    /* static: keeps the 16-bit test's ~2 KB of buffers off the 2048-byte stack */
    static Pos pos, flipped, hm;
    static i16 a[2][NNUE_N], b[2][NNUE_N];
    static i16 before[2][NNUE_N], incr[2][NNUE_N], fresh[2][NNUE_N];
    u16 *list = movebuf[28];
    i16 n, i, fail = 0, asym = 0, asymH = 0;

    parse_fen(&pos, fen ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    if (!nnue_enabled) {   /* deterministic pattern so the test needs no net file */
        i32 z;
        for (z = 0; z < (i32)NNUE_FEATURES * NNUE_N; z++)
            nn_w1[z] = (i8)((((i32)z * 7 + 13) & 255) - 128);
        for (z = 0; z < (i32)NNUE_N; z++)
            nn_b1[z] = (i8)((((i32)z * 17 + 3) & 255) - 128);
        for (z = 0; z < (i32)NNUE_W2_SIZE; z++)
            nn_w2[z] = (i8)((((i32)z * 11 + 5) & 255) - 128);
        nn_bias = 1000;
        nnue_enabled = 1;
        nnue_tables_init();
    }

    /* color symmetry: acc_black(x) must equal acc_white(rot180 + colorflip x) */
    nn_compute(&pos, a);
    flip_pos(&flipped, &pos);
    nn_compute(&flipped, b);
    for (i = 0; i < NNUE_N; i++)
        if (a[1][i] != b[0][i]) asym++;

    /* horizontal symmetry: acc(x) must equal acc(horizmirror x) in BOTH POVs
       (the per-king mirror normalization must make the eval left-right neutral) */
    hmirror_pos(&hm, &pos);
    nn_compute(&hm, b);
    for (i = 0; i < NNUE_N; i++) {
        if (a[0][i] != b[0][i]) asymH++;
        if (a[1][i] != b[1][i]) asymH++;
    }

    /* incremental make/undo round-trip for every legal move */
    nnue_reset(&pos);
    n = gen_moves(&pos, list);
    nnue_active = 1;
    {
        i16 rt = 0;   /* first-failure diagnostic flag */
        for (i = 0; i < n; i++) {
            Undo u;
            memcpy(before, nn_acc[nn_ply], sizeof before);
            do_make(&pos, list[i], &u);
            memcpy(incr, nn_acc[nn_ply], sizeof incr);
            nn_compute(&pos, fresh);
            if (memcmp(incr, fresh, sizeof incr) != 0) {
                if (!rt) {
                    i16 jj;
                    for (jj = 0; jj < 2 * NNUE_N; jj++)
                        if (((i16 *)incr)[jj] != ((i16 *)fresh)[jj]) break;
                    printf("nn rt incr-vs-fresh fail move %d (mv=%04x): acc[%d/%d] incr=%d fresh=%d\n",
                           i, list[i], jj >> 6, jj & 63, ((i16 *)incr)[jj], ((i16 *)fresh)[jj]);
                    rt = 1;
                }
                fail++;
            }
            undo_move(&pos, list[i], &u);
            if (memcmp(before, nn_acc[nn_ply], sizeof before) != 0) {
                if (!rt) {
                    i16 jj;
                    for (jj = 0; jj < 2 * NNUE_N; jj++)
                        if (((i16 *)before)[jj] != nn_acc[nn_ply][jj >> 6][jj & 63]) break;
                    printf("nn rt undo-fail move %d (mv=%04x): acc[%d/%d] before=%d after=%d\n",
                           i, list[i], jj >> 6, jj & 63, ((i16 *)before)[jj], nn_acc[nn_ply][jj >> 6][jj & 63]);
                    rt = 1;
                }
                fail++;
            }
        }
    }
    nnue_active = 0;

    nnue_reset(&pos);
    printf("nn selftest: moves=%d  acc-sym(R180)=%d  acc-sym(H)=%d  roundtrip-fails=%d  eval=%d\n",
           n, asym, asymH, fail, nnue_eval(&pos));
    return (fail == 0 && asym == 0 && asymH == 0) ? 0 : 1;
}

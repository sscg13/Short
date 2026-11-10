/* nnue.c - small NNUE evaluation: 704-feature incremental accumulators.
 *
 * Architecture (see NNUE.md):
 *   704 one-hot inputs -> two N-wide i16 accumulators (white POV, black POV)
 *   sharing ONE weight matrix, plus a shared N-wide layer-1 bias ->
 *   v5 ReLU^2 or experimental v7 LUT[clamp(acc,0,255)] -> 2N i8
 *   output weights (x64), with each product shifted by NNUE_ACT2_SHIFT ->
 *   i16 output bias (x8192) -> raw score (>> NNUE_SCALE_SHIFT = cp).
 *
 * The black POV is the white POV of the position rotated 180 degrees with
 * colors swapped, so the net is color-symmetric by construction. Both POVs
 * additionally HORIZONTALLY MIRROR every square when their own king is on the
 * e-h files, so the perspective king always lands in files a-d. Because a
 * position and its left-right mirror get the same features, the net is also
 * left-right symmetric by construction (and the 32 king buckets fold e-h ->
 * a-d without losing information).
 *
 * Incremental: do_make() records per-feature-row deltas for both accumulators.
 * PLY-INDEXED accumulator: nn_acc[ply][2][NNUE_N] is a stack of snapshots
 * indexed by the search ply (33 x 256 B, the same footprint the copy-make
 * snapshot stack used). Make reserves slot nn_ply+1 with compact pending row
 * plans. Eval materializes missing ancestors before reading the current slot;
 * positions discarded without evaluation can skip their updates. Mirror flips
 * recompute eagerly while the corresponding board is available. nnue_undo just
 * decrements nn_ply: no snapshot or restore memcpy is needed.
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
i32 c_refresh = 0;              /* materialized batch updates */
i32 c_flip = 0;                 /* mirror-flip recompute paths (nnue_make) */
#endif

#if defined(__WATCOMC__) && !defined(__386__)
/* Layer-1 weights live in a far data segment outside DGROUP. */
i8 _far nn_w1[NNUE_W1_SIZE];
#else
i8 nn_w1[NNUE_W1_SIZE];
#endif
i8 nn_b1[NNUE_N];   /* layer-1 bias (per hidden neuron, shared by both POVs) */
i16 nn_b1w[NNUE_N]; /* sign-extended copy, refilled by nnue_reset (asm rebuild) */
i8 nn_w2[NNUE_W2_SIZE];
/* v7 appends one shared curve; v8 appends 64 neuron-major curves, each
   containing 256 little-endian signed i32 entries, to the v5 layout.
   Values are bounded by +/-65025 so (curve[a]*w2)>>9 always fits an i16 table
   entry. The zero response must be zero because the DOS forward skips acc<=0.
   All other entries are independent; negative/nonmonotone maps are accepted. */
static i32 nn_curve[256]; /* one-curve streaming buffer, including on DOS */
#if defined(__WATCOMC__) && !defined(__386__)
#define NNUE_STREAM_CURVES 1
#else
/* Native arithmetic oracle retains raw curves; DOS keeps only products. */
static i32 nn_neuron_curve[NNUE_N][256];
#endif

i16 nn_bias;        /* output bias, i16, quantized at 128*64 (WORD for the asm fwd) */

#if !defined(__WATCOMC__)
/* Embed the net into the binary (gcc build: OpenBench runs the bare binary, so
   there is no runtime file dependency). The default is short-net2-finetune2.net,
   overridable at build time: OpenBench passes EVALFILE=<net> to make, which
   defines NN_EMBED_FILE=<path>; NN_STR stringifies it for the .incbin directive.
   Rebuild after re-converting a net. */
#ifndef NN_EMBED_FILE
#define NN_EMBED_FILE short-net2-finetune2.net
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
                                       [ply][POV][j]; pending plans and nn_valid
                                       distinguish unevaluated child slots.
                                       Non-static for nnue_opt.asm. */
/* Per-perspective pending operations: kind 1=add/sub, 2=capture, 3=castle.
   Mirror flips have no pending operation: their accumulator is already valid.
   33*2*8-byte plans plus 33 validity bytes = 561 bytes before alignment. */
typedef struct { i16 kind, a, b, c; } NNPlan;
NNPlan nn_plan[MAXPLY + 1][2];
static u8 nn_valid[MAXPLY + 1];
i16 nn_ply = 0;          /* current ply: index of the active accumulator slot,
                            kept in sync with the board's make/undo depth */

#if defined(__WATCOMC__) && !defined(__386__)
/* batched ply-indexed make loops (nnue_opt.asm): one 64-element copy+apply pass
   writing slot nn_ply+1. nn_make_move: dst = src + w1[to] - w1[from].
   nn_make_cap:  dst = src + w1[to] - w1[from] - w1[cap]. The src (slot nn_ply)
   is read via the _nn_ply global; dst is always src + 256 bytes. */
void nn_make_move(i16 persp, i16 to_row, i16 from_row);
void nn_make_cap(i16 persp, i16 to_row, i16 from_row, i16 cap_row);
#pragma aux nn_make_move parm [ax] [dx] [bx] modify [ax bx cx dx si di bp ds es]
#pragma aux nn_make_cap parm [ax] [dx] [bx] [cx] modify [ax bx cx dx si di bp ds es]
#define NNUE_ASM_BATCH 1

/* per-slot forward product tables (nnue_opt.asm, NNUE_OPTIMIZATION.md §5):
   fwd[p][j][a] = (curve[a] * w2[p*64+j]) >> NNUE_ACT2_SHIFT for a in [0,255]
   (bounded activation products, pre-shifted so every entry fits i16). Built at net
   load so the forward multiply becomes one word load. ONE 64 KB far array
   holding both perspectives so the linker CANNOT reorder them: the asm fwd
   reads fwd[0] at offset 0 and fwd[1] at +32768 of the same segment (a single
   2D declaration makes that layout guaranteed). */
i16 _far nn_fwd[2][NNUE_N][256];
Score nn_fwd_eval(i16 side);          /* generated asm forward via product tables */
/* Watcom otherwise assumes non-argument registers survive an external call.
   The generated routine also uses CX/DX/ES; make that contract explicit. */
#pragma aux nn_fwd_eval modify [ax cx dx es]
#ifndef NNUE_DISABLE_ASM_FWD
#define NNUE_ASM_FWD 1
#endif
/* One weight row into an accumulator: out[j] += w1[row*64+j], 64 lanes.
   The only asm body reached by the mirror-flip rebuild in nn_compute_persp;
   the C loop stays as the oracle (build with -DNNUE_DISABLE_ASM_ROWADD). */
void nn_row_add(i16 *out, u16 w1_off);
#ifndef NNUE_DISABLE_ASM_ROWADD
#define NNUE_ASM_ROWADD 1
#endif

/* The whole mirror-flip rebuild: bias copy + 0x88 board walk + inlined
   row-adds into nn_acc[slot][persp]. pm = persp | (mirror << 1).
   The C body stays as the oracle (build.ps1 -NoAsmCompute). */
void nn_compute_persp_slot(Pos *p, i16 pm, i16 slot);
#ifndef NNUE_DISABLE_ASM_COMPUTE
#define NNUE_ASM_COMPUTE 1
#endif

/* The make path's feature-row planning: fills nn_plan[nn_ply+1][0..1] and
   returns the mirror-flip mask (bit p = rebuild perspective p). Reads the
   board through SS and nn_rowtab/nn_cidxw through DS, which this binary keeps
   equal to DGROUP (see nnue_plan.asm). cap is u->cap, passed separately so the
   routine never reads a far pointer.
   The C body stays as the oracle (build.ps1 -NoAsmPlan). */
i16 nn_plan_slot(Pos *p, u16 m, i16 cap);
#ifndef NNUE_DISABLE_ASM_PLAN
#define NNUE_ASM_PLAN 1
#endif
#endif

/* The DOS scalar checks use the streamed products. Native checks independently
   multiply the stored raw per-neuron responses; cross-build tests cover both. */
static i32 nn_curve_term(i16 slot, i16 a) {
#ifdef NNUE_STREAM_CURVES
    return (i32)nn_fwd[slot >> 6][slot & 63][a];
#else
    return (nn_neuron_curve[slot & 63][a] * nn_w2[slot]) >> NNUE_ACT2_SHIFT;
#endif
}

/* ------------------------------------------------------------------ */
/* feature indexing                                                   */
/* ------------------------------------------------------------------ */

/* per-perspective mirror flags from the king squares: mirror a POV's board
   when that POV's king (after its 180 transform for persp 1) sits on e-h,
   so the own king always normalizes to files a-d. */
static void nn_mirrors(Pos *p, i16 m[2]) {
    m[0] = (p->ks[0] & 4) ? 1 : 0;                   /* white king file >= e */
    m[1] = (p->ks[1] & 4) ? 0 : 1;                   /* R180 of black king file >= e */
}

/* feature row for a color-normalized piece on a mirror-normalized compact
   square (0..703, or -1 = invalid, e.g. a pawn on rank 1/8). This is the
   pure dispatch used to build the nn_row table (and as the gcc scalar oracle
   before the table is built). */
static i16 nn_row_dispatch(i16 pc, i16 c) {
    i16 ty = TY(pc);
    if (CO(pc) == 0) {                /* own piece */
        if (ty == 6) return (c >> 3) * 4 + (c & 3);   /* dense own king: files a-d */
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
i16 nn_rowtab[15][64];          /* read directly by the asm rebuild */
static i16 nn_rowtab_ready;

/* Same lookup, inlined for the make path (nn_row is 5-6 rows of -0 code plus a
   call for what is two table reads). pm = perspective | (mirror << 1), so the
   mirror is always folded into one xor and the 180 colour swap into the piece
   xor: c ^= {0, 63, 7, 56}. Kept as a macro because Watcom -0 never inlines. */
static const u8 nn_cm[4] = { 0, 63, 7, 56 };
#define NN_ROW(pm, pc, sq88) \
    (nn_rowtab[(i16)((pc) ^ (((pm) & 1) << 3))] \
               [(i16)(((((sq88) >> 1) & 56) | ((sq88) & 7)) ^ nn_cm[(pm) & 3])])

/* Pre-folded compact index for the asm plan builder: nn_cidxw[pm][sq88] is
   ((compact(sq88) ^ nn_cm[pm]) * 2), i.e. exactly the word index into a
   64-wide row of nn_rowtab, so a row lookup is one table read plus the
   piece offset. 4*128 i16 = 1 KB near data, built once with the row table. */
i16 nn_cidxw[4][128];

static void nn_rowtab_build(void) {
    i16 pc, c, pm, sq;
    for (pc = 1; pc < 15; pc++)
        for (c = 0; c < 64; c++)
            nn_rowtab[pc][c] = nn_row_dispatch(pc, c);
    for (pm = 0; pm < 4; pm++)
        for (sq = 0; sq < 128; sq++) {
            i16 k = ((sq >> 1) & 56) | (sq & 7);
            nn_cidxw[pm][sq] = (i16)((k ^ nn_cm[pm]) * 2);
        }
    nn_rowtab_ready = 1;
}

/* feature row for a piece at sq88 in this POV, given the mirror flag */
static i16 nn_row(i16 persp, i16 pc, i16 sq88, i16 mirror) {
    i16 c = (sq88 >> 1) & 56;
    c |= sq88 & 7;
    if (persp == 1) {                 /* black POV: 180 flip + color swap */
        pc ^= 8;
        c ^= 63;
    }
    if (mirror) c ^= 7;
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
    u16 base;
    nn_mirrors(p, m);
    for (k = 0; k < NNUE_N; k++) out[k] = nn_b1[k];
    for (sq = 0; sq < 128; sq += ((sq & 7) == 7) ? 9 : 1) {
        i16 pc = p->board[sq];
        i16 row;
        if (!pc) continue;
        row = nn_row(persp, pc, sq, m[persp]);
        if (row < 0) continue;
#ifdef NNUE_ASM_ROWADD
        nn_row_add(out, (u16)row << 6);
#else
        base = (u16)row << 6;
        for (k = 0; k < NNUE_N; k++)
            out[k] += (i16)nn_w1[base + k];
#endif
    }
}

/* compute both accumulators from scratch (root of a search) */
static void nn_compute(Pos *p, i16 out[2][NNUE_N]) {
    nn_compute_persp(p, 0, out[0]);
    nn_compute_persp(p, 1, out[1]);
}

void nnue_reset(Pos *p) {
    i16 k;
    for (k = 0; k < NNUE_N; k++) nn_b1w[k] = nn_b1[k];  /* asm bias copy */
    nn_compute(p, nn_acc[0]);       /* full recompute into slot 0 */
    nn_ply = 0;
    nn_valid[0] = 3;
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

/* Materialize only the perspectives needed by an evaluation. A mirror flip
   is already computed eagerly into its child slot and is a valid chain root. */
static void nn_materialize(i16 persp) {
    i16 target = nn_ply, first = target, slot;
    i16 bit = persp + 1;
    if (nn_valid[target] & bit) return;
    while (!(nn_valid[first] & bit)) first--;
    for (slot = first + 1; slot <= target; slot++) {
        NNPlan *d = &nn_plan[slot][persp];
        nn_ply = slot - 1;
        if (d->kind == 1) { MAME_MARK(0x62); nn_batch_addsub(persp, d->a, d->b); MAME_MARK(0x63); }
        else if (d->kind == 2) { MAME_MARK(0x62); nn_batch_cap(persp, d->a, d->b, d->c); MAME_MARK(0x63); }
        else { PCOUNT(c_refresh); MAME_MARK(0x62); nn_castle_apply(persp, d->a, d->b, d->c); MAME_MARK(0x63); }
        nn_valid[slot] |= bit;
    }
    nn_ply = target;
}

/* Record the child slot's row operations (the C oracle for nnue_plan.asm).
   Returns the flip mask: bit p means perspective p must be rebuilt from
   scratch. Only a king move can change a mirror flag, and a promotion arrives
   here as a pawn, so the promo replacement below cannot mask a king. */
static i16 nn_plan_c(Pos *p, u16 m, Undo *u) {
    i16 from = mfrom(m), to = mto(m), fl = mfl(m);
    i16 mover_col = p->side ^ 1;          /* side that just moved */
    i16 mover = p->board[to];             /* mover or promo piece at 'to' */
    i16 newp = mover;
    i16 pm[2], persp, flip = 0, cap_pc = 0, cap_sq = to;
    NNPlan *d = &nn_plan[nn_ply + 1][0];

    if (ispromo(m)) mover = (mover_col == 0) ? WP : BP;   /* it was a pawn */

    /* pm = the per-perspective index mask: bit0 = perspective, bit1 = mirror
       (nn_cm folds the 180 colour swap of the black POV with the file mirror). */
    pm[0] = (p->ks[0] & 4) ? 2 : 0;       /* white POV: mirror = king on e-h */
    pm[1] = (p->ks[1] & 4) ? 1 : 3;       /* black POV: R180, so inverted */
    /* The parent flag is read from the king's FROM square (mirror = file >= e,
       and for the black POV that flips: mirror = file < e). */
    if (mover == WK) {
        if ((pm[0] >> 1) != ((from & 4) ? 1 : 0)) flip = 1;
    } else if (mover == BK) {
        if ((pm[1] >> 1) != ((from & 4) ? 0 : 1)) flip = 2;
    }
    /* Capture target hoisted out of the perspective loop: EP's victim square
       and pawn colour depend on the mover only, not on the perspective. */
    if (fl == MF_EP) {
        cap_pc = (mover_col == 0) ? BP : WP;
        cap_sq = (mover_col == 0) ? to - 16 : to + 16;
    } else if (u->cap != EMPTY) {
        cap_pc = u->cap;
    }
    for (persp = 0; persp < 2; persp++, d++) {
        i16 pmp = pm[persp];
        if (flip & (persp + 1)) {
            /* accumulator is rebuilt from scratch below */
        } else if (fl == MF_CASTLE) {
            /* castle with no mirror change: precomputed delta.
               kingside: both perspectives (own case 0, other case 1).
               queenside: only the other (nstm) perspective reaches here (the
               castling side's own perspective flipped and recomputes below). */
            d->kind = 3; d->b = mover_col; d->c = pmp >> 1;
            if (to == 0x06 || to == 0x76)      /* kingside */
                d->a = (persp == mover_col) ? 0 : 1;
            else                                 /* queenside */
                d->a = 2;
        } else {
            i16 cap_row = cap_pc ? NN_ROW(pmp, cap_pc, cap_sq) : -1;
            d->kind = (cap_row >= 0) ? 2 : 1;
            d->a = NN_ROW(pmp, newp, to);
            d->b = NN_ROW(pmp, mover, from);
            d->c = cap_row;
        }
    }
    return flip;
}

#if defined(NN_PLAN_DIFF)
i32 nn_plan_fails;   /* asm-vs-C plan mismatches (build.ps1 -PlanDiff) */
#endif

void nnue_make(Pos *p, u16 m, Undo *u) {
    i16 slot = nn_ply + 1, flip;

    PCOUNT(c_nn_make);
    nn_valid[slot] = 0;           /* never reuse an older sibling's contents */
#ifdef NNUE_ASM_PLAN
    flip = nn_plan_slot(p, m, u->cap); /* nnue_plan.asm: fills nn_plan[slot] */
#else
    flip = nn_plan_c(p, m, u);
#endif
#if defined(NN_PLAN_DIFF)
    {
        static NNPlan ref[2];
        i16 ref_flip;
        memcpy(ref, nn_plan[slot], sizeof ref);
        ref_flip = nn_plan_c(p, m, u);
        if (ref_flip != flip || memcmp(ref, nn_plan[slot], sizeof ref) != 0) {
            nn_plan_fails++;
            if (nn_plan_fails <= 4) {
                printf("plan diff mv=%04x flip asm=%d c=%d\n", (unsigned)m, flip, ref_flip);
                printf("  asm p0=%d,%d,%d,%d p1=%d,%d,%d,%d\n",
                       ref[0].kind, ref[0].a, ref[0].b, ref[0].c,
                       ref[1].kind, ref[1].a, ref[1].b, ref[1].c);
                printf("  c   p0=%d,%d,%d,%d p1=%d,%d,%d,%d\n",
                       nn_plan[slot][0].kind, nn_plan[slot][0].a, nn_plan[slot][0].b,
                       nn_plan[slot][0].c, nn_plan[slot][1].kind, nn_plan[slot][1].a,
                       nn_plan[slot][1].b, nn_plan[slot][1].c);
            }
        }
        nn_plan[slot][0] = ref[0];
        nn_plan[slot][1] = ref[1];
        flip = ref_flip;
    }
#endif
    if (flip & 1) {
        PCOUNT(c_flip);
#ifdef NNUE_ASM_COMPUTE
        nn_compute_persp_slot(p, (i16)((p->ks[0] & 4) ? 2 : 0), slot);
#else
        nn_compute_persp(p, 0, nn_acc[slot][0]);
#endif
        nn_valid[slot] |= 1;
    }
    if (flip & 2) {
        PCOUNT(c_flip);
#ifdef NNUE_ASM_COMPUTE
        nn_compute_persp_slot(p, (i16)((p->ks[1] & 4) ? 1 : 3), slot);
#else
        nn_compute_persp(p, 1, nn_acc[slot][1]);
#endif
        nn_valid[slot] |= 2;
    }
    nn_ply = slot;                /* child slot is now the active accumulator */
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
    nn_materialize(0);
    nn_materialize(1);
    PCOUNT(c_nn_eval);
#ifdef NNUE_ASM_FWD
    return nn_fwd_eval(p->side);
#else
    {
        i32 out = nn_bias;
        i16 j;
        /* act = clamp(acc, 0, 255); term = (curve[act] * w2) >> 9. The
           bounded lookup response (max magnitude 65025) is pre-shifted so the
           forward table still fits i16; out stays i32 and the final >>5
           (1.0 = 256 cp) is unchanged. stm/nstm: acc[0] is the white POV,
           acc[1] the black POV. The net is side-to-move-aware (the trainer
           always treats the side to move as "white" in feature space), so when
           black is to move the weight roles SWAP (acc[1] is the stm POV) and
           there is NO final negate - the score is already from the side to
           move's perspective. */
        for (j = 0; j < NNUE_N; j++) {
            i16 a0 = nn_acc[nn_ply][0][j], a1 = nn_acc[nn_ply][1][j];
            i16 as = (p->side == 0) ? a0 : a1;  /* stm activation */
            i16 an = (p->side == 0) ? a1 : a0;  /* nstm activation */
            i32 xs = (as < 0) ? 0 : (as > 255 ? 255 : as);
            i32 xn = (an < 0) ? 0 : (an > 255 ? 255 : an);
            out += nn_curve_term(j, (i16)xs);
            out += nn_curve_term(NNUE_N + j, (i16)xn);
        }
        return (Score)(out >> NNUE_SCALE_SHIFT);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* weight loading                                                      */
/* ------------------------------------------------------------------ */

/* Install one logical neuron's curve for both output roles. The DOS hot loop
   and its 64 KiB far product array are unchanged. Never retain a 64 KiB raw
   curve array in DGROUP: the loader reuses the same 1 KiB buffer 64 times. */
static void nn_curve_install(i16 j) {
#ifdef NNUE_STREAM_CURVES
    i16 a;
    i16 w0 = nn_w2[j], w1 = nn_w2[NNUE_N + j];
    for (a = 0; a < 256; a++) {
        nn_fwd[0][j][a] = (i16)((nn_curve[a] * w0) >> NNUE_ACT2_SHIFT);
        nn_fwd[1][j][a] = (i16)((nn_curve[a] * w1) >> NNUE_ACT2_SHIFT);
    }
#else
    memcpy(nn_neuron_curve[j], nn_curve, sizeof nn_curve);
#endif
}

/* Legacy v2 shared some own-king and pawn rows. Copy king vectors into the
   dense block without changing any feature contribution. Ascending copies
   are safe in place: every source is above its destination, and pawn rows
   (32+) are never overwritten. No temporary far/near allocation is needed. */
static void nn_remap_legacy(void) {
    i16 rank, file;
    for (rank = 1; rank < 8; rank++)
        for (file = 0; file < 4; file++) {
            u16 dst = (u16)(rank * 4 + file) << 6;
            u16 src = (u16)(rank * 8 + file) << 6;
            memcpy(nn_w1 + dst, nn_w1 + src, NNUE_N);
        }
}

static void nn_curve_default(void) {
    i16 a, j;
    for (a = 0; a < 256; a++) nn_curve[a] = (i32)a * a;
    for (j = 0; j < NNUE_N; j++) nn_curve_install(j);
}

static int nn_curve_valid(void) {
    i16 a;
    if (nn_curve[0] != 0) return 0;
    for (a = 1; a < 256; a++)
        if (nn_curve[a] < -65025L || nn_curve[a] > 65025L) return 0;
    return 1;
}

#ifndef __WATCOMC__
/* Native embedded blob parser. DOS streams curves directly from the file. */
static int nnue_parse_blob(const u8 *p, i32 len) {
    u16 ver, feats, hN;
    i32 w1sz = (i32)NNUE_FEATURES * NNUE_N;
    i32 need = 12 + w1sz + NNUE_N + NNUE_W2_SIZE + 2;
    if (len < need) return 0;
    if (p[0] != 'N' || p[1] != 'N' || p[2] != 'U' || p[3] != 'E') return 0;
    ver   = (u16)p[4] | ((u16)p[5] << 8);
    feats = (u16)p[6] | ((u16)p[7] << 8);
    hN    = (u16)p[8] | ((u16)p[9] << 8);
    if (ver != NNUE_NET_VERSION && ver != 7 && ver != 5 && ver != NNUE_LEGACY_VERSION) return 0;
    if (ver == 7 && len != need + 1024) return 0;
    if (ver == 8 && len != need + (i32)NNUE_N * 1024L) return 0;
    if (p[10] != 0 || p[11] != 0) return 0;
    if (feats != NNUE_FEATURES || hN != NNUE_N) return 0;
    memcpy(nn_w1, p + 12, (size_t)w1sz);
    memcpy(nn_b1, p + 12 + w1sz, (size_t)NNUE_N);
    memcpy(nn_w2, p + 12 + w1sz + NNUE_N, (size_t)NNUE_W2_SIZE);
    nn_bias = (i16)((u16)(u8)p[12 + w1sz + NNUE_N + NNUE_W2_SIZE]
            | ((u16)(u8)p[13 + w1sz + NNUE_N + NNUE_W2_SIZE] << 8));
    if (ver == 7 || ver == 8) {
        i16 a, j;
        for (j = 0; j < NNUE_N; j++) {
            i32 offset = ver == 8 ? (i32)j * 1024L : 0;
            for (a = 0; a < 256; a++) {
                const u8 *q = p + need + offset + (i32)a * 4;
                nn_curve[a] = (i32)((u32)q[0] | ((u32)q[1] << 8) |
                                   ((u32)q[2] << 16) | ((u32)q[3] << 24));
            }
            if (!nn_curve_valid()) return 0;
            nn_curve_install(j);
        }
    } else nn_curve_default();
    if (ver == NNUE_LEGACY_VERSION) nn_remap_legacy();
    nnue_enabled = 1;
    nnue_tables_init();
    return 1;
}
#endif

/* ------------------------------------------------------------------ */
/* one-time table builds (dedicated init: all precomputation/setup     */
/* ------------------------------------------------------------------ */

/* Curves/products are installed while loading, after nn_w2 is available.
   Finish the feature-row lookup and castling deltas here. */
void nnue_tables_init(void) {
    nn_rowtab_build();
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
    if (ver != NNUE_NET_VERSION && ver != 7 && ver != 5 && ver != NNUE_LEGACY_VERSION) { fclose(f); return 0; }
    if (hdr[10] != 0 || hdr[11] != 0) { fclose(f); return 0; }
    if (feats != NNUE_FEATURES || hN != NNUE_N) { fclose(f); return 0; }
    if (fread(nn_w1, 1, (size_t)w1sz, f) != (size_t)w1sz) { fclose(f); return 0; }
    if (fread(nn_b1, 1, (size_t)NNUE_N, f) != NNUE_N) { fclose(f); return 0; }
    if (fread(nn_w2, 1, (size_t)NNUE_W2_SIZE, f) != NNUE_W2_SIZE) { fclose(f); return 0; }
    if (fread(hdr, 1, 2, f) != 2) { fclose(f); return 0; }
    nn_bias = (i16)((u16)(u8)hdr[0] | ((u16)(u8)hdr[1] << 8));
    if (ver == 7 || ver == 8) {
        i16 j;
        for (j = 0; j < NNUE_N; j++) {
            if ((ver == 8 || j == 0) &&
                (fread(nn_curve, 4, 256, f) != 256 || !nn_curve_valid())) {
                fclose(f);
                return 0;
            }
            nn_curve_install(j);
        }
        if (fgetc(f) != EOF) { fclose(f); return 0; }
    } else nn_curve_default();
    fclose(f);
    if (ver == NNUE_LEGACY_VERSION) nn_remap_legacy();
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
   short-net2-finetune2.net - the path below is ignored there). The 16-bit build has
   no embedded net and ships the net as the FAT 8.3 blob CHESS.NET on the emulator
   floppies (the official net's long name is not valid 8.3, so it loads chess.net). */
int nnue_ensure_default(void) {
#if defined(__WATCOMC__)
    return nnue_ensure_loaded("chess.net");
#else
    return nnue_ensure_loaded("short-net2-finetune2.net");
#endif
}

/* ------------------------------------------------------------------ */
/* self-test: `chess nn [fen]`                                        */
/* ------------------------------------------------------------------ */

#ifdef NN_TEST
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

#endif /* NN_TEST helpers */

/* direct per-call cost measurement (`chess nbench`): times nnue_eval and a
   make/undo pair in loops, prints ms per 1000 calls. Convert to cycles with the
   host clock (8088 16 MHz: cyc = ms*16 per call; 286 6 MHz: cyc = ms*6). */
int nnue_bench(void) {
    static Pos pos;
    static u16 list[256];
    Undo u;
    i16 i, n, iters;
    clock_t t0, t1;
    i32 eval_ms, delta_ms, capture_ms;

    parse_fen(&pos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    nnue_reset(&pos);
    nnue_active = 1;
    n = gen_moves(&pos, list);
    if (n <= 0) return 1;

    iters = 4000;
    t0 = engine_clock();
    MAME_MARK(0x20);
    for (i = 0; i < iters; i++) nnue_eval(&pos);
    MAME_MARK(0x21);
    t1 = engine_clock();
#ifdef __WATCOMC__
    /* engine_clock() is 32-bit milliseconds; each calibration block is
       shorter than 65.536 s, so the low 16 bits of the difference are exact
       and the multiply stays 16-bit cheap on the target. */
    eval_ms = (i32)(u16)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
#else
    eval_ms = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
#endif

    iters = 10000;
    {
        u16 qm = 0;
        for (i = 0; i < n; i++)
            if (mfl(list[i]) == 0 && pos.board[mto(list[i])] == EMPTY) {
                qm = list[i];
                break;
            }
        if (!qm) return 1;
        t0 = engine_clock();
        MAME_MARK(0x22);
#ifdef PROFILE
        {
            i32 r0 = c_refresh, f0 = c_flip;
#endif
        for (i = 0; i < iters; i++) {
            do_make(&pos, qm, &u);
            nn_materialize(0); nn_materialize(1);
            undo_move(&pos, qm, &u);
        }
        MAME_MARK(0x23);
#ifdef PROFILE
            printf("nbench applies=%ld flips=%ld\n", (long)(c_refresh - r0), (long)(c_flip - f0));
        }
#endif
        t1 = engine_clock();
#ifdef __WATCOMC__
        delta_ms = (i32)(u16)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
#else
        delta_ms = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
#endif
    }

    /* Lazy builds force materialization inside both make/undo loops, so these
       timings include planning plus applying the deltas, not just recording.
       A distinct capture workload exercises the three-row batch routine.
       Keep its timer and marker separate from the historical make/undo pair. */
    {
        u16 cm = 0;
        for (i = 0; i < n; i++)
            if (pos.board[mto(list[i])] != EMPTY && mfl(list[i]) == 0) {
                cm = list[i];
                break;
            }
        if (!cm) return 1;
        t0 = engine_clock();
        MAME_MARK(0x24);
        for (i = 0; i < iters; i++) {
            do_make(&pos, cm, &u);
            nn_materialize(0); nn_materialize(1);
            undo_move(&pos, cm, &u);
        }
        MAME_MARK(0x25);
        t1 = engine_clock();
#ifdef __WATCOMC__
        capture_ms = (i32)(u16)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
#else
        capture_ms = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
#endif
    }

    /* Separately measure planning plus board make/undo without evaluation.
       Subtract these from the materialized loops above to estimate apply cost. */
    {
        i16 kind;
        i32 plan_ms[2];
        for (kind = 0; kind < 2; kind++) {
            u16 pm = 0;
            for (i = 0; i < n; i++)
                if (mfl(list[i]) == 0 &&
                    (pos.board[mto(list[i])] != EMPTY) == kind) {
                    pm = list[i]; break;
                }
            if (!pm) return 1;
            t0 = engine_clock();
            MAME_MARK(kind ? 0x28 : 0x26);
            for (i = 0; i < iters; i++) {
                do_make(&pos, pm, &u);
                undo_move(&pos, pm, &u);
            }
            MAME_MARK(kind ? 0x29 : 0x27);
            t1 = engine_clock();
#ifdef __WATCOMC__
            plan_ms[kind] = (i32)(u16)(t1 - t0) * 1000 / CLOCKS_PER_SEC;
#else
            plan_ms[kind] = ((i32)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
#endif
        }
        printf("nbench plan_raw_ms quiet=%ld capture=%ld\n",
               (long)plan_ms[0], (long)plan_ms[1]);
    }

    printf("nbench eval1000=%ld delta1000=%ld\n",
           (long)(eval_ms * 1000 / 4000), (long)(delta_ms * 1000 / 10000));
    printf("nbench capture1000=%ld\n", (long)(capture_ms * 1000 / 10000));
#ifdef TIMING_DETAIL
    printf("nbench raw_ms eval=%ld delta=%ld capture=%ld\n",
           (long)eval_ms, (long)delta_ms, (long)capture_ms);
#endif
    nnue_active = 0;
    return 0;
}

#ifdef NN_TEST
/* Exercise chains whose parents have never been evaluated, sibling reuse,
   horizontal king flips in both directions, castling, EP, promotions and nulls. */
static i16 nn_chain_check(Pos *p, i16 fresh[2][NNUE_N]) {
    static const char *fens[4] = {
        "4k3/8/8/8/8/8/8/3K4 w - - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "8/P7/3k4/8/8/8/7p/4K3 w - - 0 1"
    };
    static const u16 moves[4][4] = {
        { MK(0x04,0x03,0,0), MK(0x73,0x74,0,0), MK(0x03,0x04,0,0), MK(0x74,0x73,0,0) },
        { MK(0x06,0x04,MF_CASTLE,0), MK(0x72,0x74,MF_CASTLE,0), MK(0x15,0x05,0,0), MK(0x63,0x73,0,0) },
        { MK(0x53,0x44,MF_EP,0), MK(0x65,0x74,0,0), MK(0x15,0x04,0,0), MK(0x54,0x65,0,0) },
        { MK(0x70,0x60,0,WN), MK(0x07,0x17,0,BN), MK(0x13,0x04,0,0), MK(0x54,0x53,0,0) }
    };
    static Undo undo[4];
    i16 c, pass, step, turn, j, saved_ep = -1, fails = 0;
    for (c = 0; c < 4; c++) {
        parse_fen(p, fens[c]);
        nnue_reset(p);
        for (pass = 0; pass < 2; pass++) {
            for (step = 0; step < 4; step++) {
                do_make(p, moves[c][step], &undo[step]);
                if (is_attacked(p, p->ks[p->side ^ 1], p->side)) fails++;
            }
            for (step = 4; step >= 0; step--) {
                if (step == 4 || step == 2 || step == 0) {
                    nn_compute(p, fresh);
                    for (turn = 0; turn < 2; turn++) {
                        i32 out = nn_bias;
                        Score got = nnue_eval(p);
                        if (memcmp(fresh, nn_acc[nn_ply], sizeof nn_acc[0])) fails++;
                        for (j = 0; j < 2 * NNUE_N; j++) {
                            i32 a = fresh[p->side ^ (j >> 6)][j & 63];
                            if (a < 0) a = 0;
                            if (a > 255) a = 255;
                            out += nn_curve_term(j, (i16)a);
                        }
                        if (got != (Score)(out >> NNUE_SCALE_SHIFT)) fails++;
                        if (!turn) saved_ep = nm_make(p); else nm_undo(p, saved_ep);
                    }
                }
                if (step) undo_move(p, moves[c][step - 1], &undo[step - 1]);
            }
        }
    }
    return fails;
}

/* Make/materialize/undo round-trip for every legal move of *p: the incremental
   accumulator must equal a fresh recompute, and undo must restore the parent
   slot byte for byte. Returns the failure count (diagnostics print once). */
static i16 nn_roundtrip(Pos *p, i16 before[2][NNUE_N], i16 incr[2][NNUE_N],
                        i16 fresh[2][NNUE_N]) {
    u16 *list = movebuf[28];
    i16 n = gen_moves(p, list), i, fail = 0, rt = 0;
    for (i = 0; i < n; i++) {
        Undo u;
        memcpy(before, nn_acc[nn_ply], sizeof(i16) * 2 * NNUE_N);
        do_make(p, list[i], &u);
        nn_materialize(0); nn_materialize(1);
        memcpy(incr, nn_acc[nn_ply], sizeof(i16) * 2 * NNUE_N);
        nn_compute(p, fresh);
        if (memcmp(incr, fresh, sizeof(i16) * 2 * NNUE_N) != 0) {
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
        undo_move(p, list[i], &u);
        if (memcmp(before, nn_acc[nn_ply], sizeof(i16) * 2 * NNUE_N) != 0) {
            if (!rt) {
                i16 jj;
                for (jj = 0; jj < 2 * NNUE_N; jj++)
                    if (((i16 *)before)[jj] != nn_acc[nn_ply][jj >> 6][jj & 63]) break;
                printf("nn rt undo-fail move %d (mv=%04x): acc[%d/%d] before=%d after=%d\n",
                       i, list[i], jj >> 6, jj & 63, ((i16 *)before)[jj],
                       nn_acc[nn_ply][jj >> 6][jj & 63]);
                rt = 1;
            }
            fail++;
        }
    }
    return fail;
}

int nnue_selftest(const char *fen) {
    /* static: keeps the 16-bit test's ~2 KB of buffers off the search stack */
    static Pos pos, flipped, hm;
    static i16 a[2][NNUE_N], b[2][NNUE_N];
    static i16 before[2][NNUE_N], incr[2][NNUE_N], fresh[2][NNUE_N];
    u16 *list = movebuf[28];
    i16 n, i, fail = 0, asym = 0, asymH = 0, fwd_fail = 0, map_fail = 0;

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
        nn_curve_default();
        nnue_enabled = 1;
        nnue_tables_init();
    }

    /* All 32 normalized own-king squares must occupy their own dense block,
       including ranks 5-8 which formerly selected pawn weights. */
    {
        i16 rank, file, row = 0;
        for (rank = 0; rank < 8; rank++)
            for (file = 0; file < 4; file++, row++)
                if (nn_rowtab[WK][rank * 8 + file] != row) map_fail++;
        if (nn_rowtab[WK][35] == nn_rowtab[WP][11]) map_fail++;
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
    fail += nn_roundtrip(&pos, before, incr, fresh);
    {
        /* Every move type the plan/make path can see: castling on both wings,
           captures, en passant, promotions, and king moves that flip a mirror
           (kiwipete and friends; small enough for the 16-bit test). */
        static const char *const fens[] = {
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 2",
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "4k3/1P4P1/8/8/8/8/1p4p1/4K3 w - - 0 1",
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
        };
        i16 k;
        for (k = 0; k < (i16)(sizeof fens / sizeof fens[0]); k++) {
            parse_fen(&pos, fens[k]);
            nnue_reset(&pos);
            fail += nn_roundtrip(&pos, before, incr, fresh);
        }
    }
    {
        i16 chain_fail = nn_chain_check(&pos, fresh);
        if (chain_fail) printf("nn chain-fails=%d\n", chain_fail);
        fail += chain_fail;
        parse_fen(&pos, fen ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        nnue_reset(&pos);
    }
    nnue_active = 0;

    /* Compare the generated forward pass with scalar arithmetic at both clamp
       boundaries, both POV orders, and signed accumulator extremes. Rotating
       the values exercises every output weight at each activation boundary. */
    {
        static const i16 values[8] = { -32768, -1, 0, 1, 254, 255, 256, 32767 };
        i16 pattern, side, j, saved_side = pos.side;
        for (pattern = 0; pattern < 8; pattern++) {
            for (j = 0; j < 2 * NNUE_N; j++)
                nn_acc[nn_ply][j / NNUE_N][j % NNUE_N] = values[(j + pattern) & 7];
            for (side = 0; side < 2; side++) {
                i32 out = nn_bias;
                Score got;
                for (j = 0; j < 2 * NNUE_N; j++) {
                    i32 act = nn_acc[nn_ply][side ^ (j / NNUE_N)][j % NNUE_N];
                    if (act < 0) act = 0;
                    if (act > 255) act = 255;
                    out += nn_curve_term(j, (i16)act);
                }
#ifdef NNUE_ASM_FWD
                got = nn_fwd_eval(side);
#else
                pos.side = side;
                got = nnue_eval(&pos);
#endif
                if (got != (Score)(out >> NNUE_SCALE_SHIFT)) {
                    if (!fwd_fail)
                        printf("nn forward mismatch pattern=%d side=%d got=%d expected=%d\n",
                               pattern, side, got, (Score)(out >> NNUE_SCALE_SHIFT));
                    fwd_fail++;
                }
            }
        }
        pos.side = saved_side;
    }

    nnue_reset(&pos);
    printf("nn selftest: moves=%d  acc-sym(R180)=%d  acc-sym(H)=%d  roundtrip-fails=%d  fwd-fails=%d  map-fails=%d  eval=%d\n",
           n, asym, asymH, fail, fwd_fail, map_fail, nnue_eval(&pos));
#if defined(NN_PLAN_DIFF)
    printf("nn plan-diff fails=%ld (asm plan vs C oracle)\n", (long)nn_plan_fails);
    if (nn_plan_fails) return 1;
#endif
    return (fail == 0 && asym == 0 && asymH == 0 && fwd_fail == 0 && map_fail == 0) ? 0 : 1;
}

#endif /* NN_TEST */

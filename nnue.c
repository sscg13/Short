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
 * Incremental: do_make() applies per-feature-row deltas to both accumulators;
 * nnue_make snapshots the pre-move accumulator onto a per-ply stack slot and
 * nnue_undo restores it (copy-make), so no delta record is kept. The big weight
 * matrix lives in a far segment on 16-bit. A king move that crosses the d/e file
 * boundary flips a POV's mirror flag (every piece re-indexes), so that POV is
 * recomputed from scratch instead of applying deltas.
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

int nnue_enabled = 0;
int nnue_active = 0;

#if defined(PROFILE) || defined(VCLOCK)
long c_nn_make = 0;              /* nnue_make entries */
long c_nn_undo = 0;              /* nnue_undo entries */
long c_nn_eval = 0;              /* nnue_eval entries */
long c_refresh = 0;              /* feature-row deltas applied (nn_delta_apply) */
long c_flip = 0;                 /* mirror-flip recompute paths (nnue_make) */
#endif

#if defined(__WATCOMC__) && !defined(__386__)
/* 45 KB won't fit the ~23 KB of free near data; put it in a far data segment. */
signed char _far nn_w1[NNUE_W1_SIZE];
#else
signed char nn_w1[NNUE_W1_SIZE];
#endif
signed char nn_b1[NNUE_N];   /* layer-1 bias (per hidden neuron, shared by both POVs) */
signed char nn_w2[NNUE_W2_SIZE];
int nn_bias;                 /* output bias, i16, quantized at 128*64 */

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
extern const unsigned char nn_embedded_net_start[];
extern const unsigned char nn_embedded_net_end[];
#endif

short nn_acc[2][NNUE_N];   /* current pre-activations, white POV / black POV
                              (non-static so nnue_opt.asm can reference it) */

#if defined(__WATCOMC__) && !defined(__386__)
/* hand-unrolled 64-element apply loops (nnue_opt.asm) for the 16-bit build:
   acc[persp][j] += (short)w1[row*64+j]  /  -=  for j in 0..63 */
void nn_apply_add(int persp, int row);
void nn_apply_sub(int persp, int row);
#define NNUE_ASM_APPLY 1

/* batched make loops (nnue_opt.asm): one 64-element pass with a single
   word-RMW per element. nn_make_move: acc += w1[to] - w1[from].
   nn_make_cap:  acc += w1[to] - w1[from] - w1[cap]. */
void nn_make_move(int persp, int to_row, int from_row);
void nn_make_cap(int persp, int to_row, int from_row, int cap_row);
#define NNUE_ASM_BATCH 1

/* per-slot forward product tables (nnue_opt.asm, NNUE_OPTIMIZATION.md §5):
   fwd[p][j][a+128] = w2[p*64+j] * a  for a in [-128,127]. Built at net load so
   the forward multiply becomes one word load + one shl. ONE 64 KB far array
   holding both perspectives so the linker CANNOT reorder them: the asm fwd
   reads fwd[0] at offset 0 and fwd[1] at +32768 of the same segment (a single
   2D declaration makes that layout guaranteed). */
short _far nn_fwd[2][NNUE_N][256];
Score nn_fwd_eval(int side);          /* hand-asm forward pass */
#ifndef NNUE_DISABLE_ASM_FWD
#define NNUE_ASM_FWD 1
#endif
#endif

/* copy-make accumulator stack: nnue_make snapshots the pre-move accumulator into
   the current ply's slot before applying deltas, and nnue_undo restores it. Undo
   is one 256-byte copy instead of reversing the applied (row, sign) deltas, so a
   make/undo pair costs half the NNUE apply work. 33*2*64*2 = 8.4 KB near data;
   nn_acc stays the live accumulator (the hand-asm apply/fwd loops reference it by
   a fixed offset), so the stack is plain near memcpy on both builds. */
static short nn_save[MAXPLY + 1][2][NNUE_N];
static int nn_ply;

static void nn_save_acc(int ply) {
    memcpy(nn_save[ply], nn_acc, sizeof nn_acc);
}

static void nn_restore_acc(int ply) {
    memcpy(nn_acc, nn_save[ply], sizeof nn_acc);
}

/* ------------------------------------------------------------------ */
/* feature indexing                                                   */
/* ------------------------------------------------------------------ */

/* per-perspective mirror flags from the king squares: mirror a POV's board
   when that POV's king (after its 180 transform for persp 1) sits on e-h,
   so the own king always normalizes to files a-d. */
static void nn_mirrors(Pos *p, int m[2]) {
    m[0] = ((sq2c(p->ks[0]) & 7) >= 4) ? 1 : 0;        /* white king file >= e */
    m[1] = ((7 - (sq2c(p->ks[1]) & 7)) >= 4) ? 1 : 0;  /* R180 of black king file >= e */
}

/* feature row for a color-normalized piece on a mirror-normalized compact
   square (0..703, or -1 = invalid, e.g. a pawn on rank 1/8). This is the
   pure dispatch used to build the nn_row table (and as the gcc scalar oracle
   before the table is built). */
static int nn_row_dispatch(int pc, int c) {
    int ty = TY(pc);
    if (CO(pc) == 0) {                /* own piece */
        if (ty == 6) return (c >> 3) * 8 + (c & 7);   /* own king: file <= 3 by mirror */
        if (ty == 1) { int r = c >> 3; if (r < 1 || r > 6) return -1; return 32 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 80 + (ty - 2) * 64 + c;
        return -1;
    } else {                          /* enemy piece */
        if (ty == 6) return 336 + c;
        if (ty == 1) { int r = c >> 3; if (r < 1 || r > 6) return -1; return 400 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 448 + (ty - 2) * 64 + c;
        return -1;
    }
}

/* nn_row lookup table: [pc][normalized compact square] -> row or -1. Built
   once from nn_row_dispatch; replaces the branchy per-call dispatch (nn_row is
   called once per feature apply, 114,921x in profile-1). 15x64 i16 = 1.9 KB
   near data (DGROUP has ~10 KB headroom). */
static short nn_rowtab[15][64];
static int nn_rowtab_ready;

static void nn_rowtab_build(void) {
    int pc, c;
    for (pc = 1; pc < 15; pc++)
        for (c = 0; c < 64; c++)
            nn_rowtab[pc][c] = (short)nn_row_dispatch(pc, c);
    nn_rowtab_ready = 1;
}

/* feature row for a piece at sq88 in this POV, given the mirror flag */
static int nn_row(int persp, int pc, int sq88, int mirror) {
    int c = sq2c(sq88);
    if (persp == 1) {                 /* black POV: 180 flip + color swap */
        pc = CO(pc) ? pc - 8 : pc + 8;
        c = 63 - c;
    }
    if (mirror) c = (c & 0xF8) | (7 - (c & 7));
    return nn_rowtab[pc][c];
}

/* Batched feature-row apply (make path). nn_batch_addsub applies +w1[to] -
   w1[from] in ONE pass (one word-RMW per element, both on the 16-bit asm path
   and the scalar C oracle); nn_batch_cap applies +w1[to] - w1[from] -
   w1[cap]. Bit-identical to sequential sub+add (i16 arithmetic mod 2^16),
   so node counts are unchanged by construction. A negative row means the
   piece has no feature (e.g. a pawn on the promotion rank) -> skip it. */
static void nn_batch_addsub(int persp, int to_row, int from_row) {
    if (to_row < 0 || from_row < 0) return;
    PCOUNT(c_refresh);
#ifdef NNUE_ASM_BATCH
    nn_make_move(persp, to_row, from_row);
#else
    {
        unsigned int b0 = (unsigned)to_row << 6, b1 = (unsigned)from_row << 6;
        int j;
        for (j = 0; j < NNUE_N; j++)
            nn_acc[persp][j] += (short)nn_w1[b0 + j] - (short)nn_w1[b1 + j];
    }
#endif
}

static void nn_batch_cap(int persp, int to_row, int from_row, int cap_row) {
    if (to_row < 0 || from_row < 0 || cap_row < 0) return;
    PCOUNT(c_refresh);
#ifdef NNUE_ASM_BATCH
    nn_make_cap(persp, to_row, from_row, cap_row);
#else
    {
        unsigned int b0 = (unsigned)to_row << 6, b1 = (unsigned)from_row << 6;
        unsigned int b2 = (unsigned)cap_row << 6;
        int j;
        for (j = 0; j < NNUE_N; j++)
            nn_acc[persp][j] += (short)nn_w1[b0 + j] - (short)nn_w1[b1 + j]
                              - (short)nn_w1[b2 + j];
    }
#endif
}

/* compute one perspective's accumulator from scratch (init with the layer-1 bias) */
static void nn_compute_persp(Pos *p, int persp, short *out) {
    int m[2], sq, k;
    nn_mirrors(p, m);
    for (k = 0; k < NNUE_N; k++) out[k] = nn_b1[k];
    for (sq = 0; sq < 128; sq++) {
        int pc = p->board[sq];
        int row;
        unsigned int base;
        if (!pc) continue;
        row = nn_row(persp, pc, sq, m[persp]);
        if (row < 0) continue;
        base = (unsigned)row << 6;
        for (k = 0; k < NNUE_N; k++)
            out[k] += (short)nn_w1[base + k];
    }
}

/* compute both accumulators from scratch (root of a search) */
static void nn_compute(Pos *p, short out[2][NNUE_N]) {
    nn_compute_persp(p, 0, out[0]);
    nn_compute_persp(p, 1, out[1]);
}

void nnue_reset(Pos *p) {
    nn_compute(p, nn_acc);
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
static short cast_delta[3][2][2][NNUE_N];

static void nn_castle_build(void) {
    /* (king from, king to, rook from, rook to) as 0x88 squares, per color.
       The perspective that uses each case is: case 0 = the castling side's
       OWN perspective; cases 1-2 = the OTHER (nstm) perspective. Each delta
       is built with the actual persp, pieces and squares of that color. */
    static const int ks_sq[2][2][4] = {
        /* O-O */
        { { 0x04, 0x06, 0x07, 0x05 },   /* white: e1->g1, h1->f1 */
          { 0x74, 0x76, 0x77, 0x75 } }, /* black: e8->g8, h8->f8 */
        /* O-O-O */
        { { 0x04, 0x02, 0x00, 0x03 },   /* white: e1->c1, a1->d1 */
          { 0x74, 0x72, 0x70, 0x73 } }, /* black: e8->c8, a8->d8 */
    };
    static const int case_cast[3] = { 0, 0, 1 };   /* 0=O-O, 1=O-O-O */
    int c, w, m, j;

    for (c = 0; c < 3; c++) {
        for (w = 0; w < 2; w++) {
            const int *s = ks_sq[case_cast[c]][w];
            int persp = (c == 0) ? w : (w ^ 1);   /* case 0: own persp; 1-2: nstm */
            int king = (w == 0) ? WK : BK;
            int rook = (w == 0) ? WR : BR;
            for (m = 0; m < 2; m++) {
                int kf = nn_row(persp, king, s[0], m);
                int kt = nn_row(persp, king, s[1], m);
                int rf = nn_row(persp, rook, s[2], m);
                int rt = nn_row(persp, rook, s[3], m);
                for (j = 0; j < NNUE_N; j++) {
                    short a = 0;
                    if (kt >= 0) a += (short)nn_w1[((unsigned)kt << 6) + j];
                    if (kf >= 0) a -= (short)nn_w1[((unsigned)kf << 6) + j];
                    if (rt >= 0) a += (short)nn_w1[((unsigned)rt << 6) + j];
                    if (rf >= 0) a -= (short)nn_w1[((unsigned)rf << 6) + j];
                    cast_delta[c][w][m][j] = a;
                }
            }
        }
    }
}

/* apply the precomputed castle delta to one perspective (mirror unchanged) */
static void nn_castle_apply(int persp, int case_idx, int mover_col, int mirror) {
    int j;
    const short *d = cast_delta[case_idx][mover_col][mirror];
    for (j = 0; j < NNUE_N; j++)
        nn_acc[persp][j] += d[j];
}

void nnue_make(Pos *p, unsigned int m, Undo *u) {
    int from = mfrom(m), to = mto(m), fl = mfl(m);
    int mover_col = p->side ^ 1;          /* side that just moved */
    int mover = p->board[to];             /* mover or promo piece at 'to' */
    int newp = mover;
    int persp, mpost[2], mpre[2], flip[2];

    PCOUNT(c_nn_make);
    if (ispromo(m)) mover = (mover_col == 0) ? WP : BP;   /* it was a pawn */

    /* snapshot the pre-move accumulator (copy-make: undo restores from the stack) */
    nn_save_acc(nn_ply);

    /* mirror flags before/after this move (only a king move can change them) */
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
            int case_idx;
            if (to == 0x06 || to == 0x76)      /* kingside */
                case_idx = (persp == mover_col) ? 0 : 1;
            else                                 /* queenside */
                case_idx = 2;
            PCOUNT(c_refresh);
            nn_castle_apply(persp, case_idx, mover_col, mpost[persp]);
        } else {
            int to_row, from_row, cap_row = -1;
            to_row   = nn_row(persp, newp, to, mpost[persp]);
            from_row = nn_row(persp, mover, from, mpost[persp]);
            if (fl == MF_EP) {
                int esq = (mover_col == 0) ? to - 16 : to + 16;
                int ep = (mover_col == 0) ? BP : WP;
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
        if (flip[persp]) nn_compute_persp(p, persp, nn_acc[persp]);
    nn_ply++;
}

void nnue_undo(Pos *p) {
    PCOUNT(c_nn_undo);
    (void)p;
    if (nn_ply > 0) nn_ply--;
    nn_restore_acc(nn_ply);   /* copy-make: no delta reversal, no recompute */
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
        long out = nn_bias;
        int j;
        /* stm/nstm: acc[0] is the white POV, acc[1] the black POV. The net is
           side-to-move-aware (the trainer always treats the side to move as
           "white" in feature space), so when black is to move the weight roles
           SWAP (acc[1] is the stm POV) and there is NO final negate - the score
           is already from the side to move's perspective. */
        for (j = 0; j < NNUE_N; j++) {
            int a0 = nn_acc[0][j], a1 = nn_acc[1][j];
            int ws = nn_w2[j];                  /* stm weight */
            int wn = nn_w2[NNUE_N + j];         /* nstm weight */
            int as = (p->side == 0) ? a0 : a1;  /* stm activation */
            int an = (p->side == 0) ? a1 : a0;  /* nstm activation */

            if (as >= 128)       out += (long)(ws << 7);
            else if (as <= -128) out -= (long)(ws << 7);
            else                 out += (long)(as * ws);

            if (an >= 128)       out += (long)(wn << 7);
            else if (an <= -128) out -= (long)(wn << 7);
            else                 out += (long)(an * wn);
        }
        return (Score)(out >> NNUE_SCALE_SHIFT);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* weight loading                                                      */
/* ------------------------------------------------------------------ */

#ifdef NNUE_ASM_FWD
/* fill fwd[p][j][a+128] = w2[p*64+j] * a (a=-128..127) from the current nn_w2.
   Entries are consecutive +w, so this is 32k far word stores, one-time. */
static void nn_fwd_build(void) {
    int j, a;
    for (j = 0; j < NNUE_N; j++) {
        int w0 = nn_w2[j];
        int w1 = nn_w2[NNUE_N + j];
        short v0 = (short)(-128 * w0);
        short v1 = (short)(-128 * w1);
        for (a = 0; a < 256; a++) {
            nn_fwd[0][j][a] = v0;
            nn_fwd[1][j][a] = v1;
            v0 = (short)(v0 + w0);
            v1 = (short)(v1 + w1);
        }
    }
}
#endif

/* parse a net blob (engine format, see NNUE.md) from memory */
static int nnue_parse_blob(const unsigned char *p, long len) {
    unsigned int feats, hN;
    long w1sz = (long)NNUE_FEATURES * NNUE_N;
    long need = 12 + w1sz + NNUE_N + NNUE_W2_SIZE + 2;
    if (len < need) return 0;
    if (p[0] != 'N' || p[1] != 'N' || p[2] != 'U' || p[3] != 'E') return 0;
    if (p[4] != 1 || p[5] != 0) return 0;                       /* version 1 */
    feats = (unsigned int)p[6] | ((unsigned int)p[7] << 8);
    hN    = (unsigned int)p[8] | ((unsigned int)p[9] << 8);
    if (feats != NNUE_FEATURES || hN != NNUE_N) return 0;
    memcpy(nn_w1, p + 12, (size_t)w1sz);
    memcpy(nn_b1, p + 12 + w1sz, (size_t)NNUE_N);
    memcpy(nn_w2, p + 12 + w1sz + NNUE_N, (size_t)NNUE_W2_SIZE);
    nn_bias = (int)(unsigned char)p[12 + w1sz + NNUE_N + NNUE_W2_SIZE]
            | ((int)(unsigned char)p[13 + w1sz + NNUE_N + NNUE_W2_SIZE] << 8);
    if ((unsigned)nn_bias > 32767) nn_bias -= 65536;            /* sign-extend i16 */
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

int nnue_load(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len, got;
    int ok;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    len = ftell(f);
    if (len <= 0 || len > 200000L) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (unsigned char *)malloc((size_t)len);
    if (!buf) { fclose(f); return 0; }
    got = (long)fread(buf, 1, (size_t)len, f);
    fclose(f);
    ok = (got == len) && nnue_parse_blob(buf, len);
    free(buf);
    return ok;
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
                           (long)(nn_embedded_net_end - nn_embedded_net_start));
#endif
}
#endif

/* ensure the default net is loaded; idempotent, so it is safe to call at every
   search entry point (bench/think/search_root/profile) to make NNUE the default */
int nnue_ensure_default(void) {
    return nnue_ensure_loaded("chess.net");
}

/* ------------------------------------------------------------------ */
/* self-test: `chess nn [fen]`                                        */
/* ------------------------------------------------------------------ */

static void flip_pos(Pos *dst, const Pos *src) {
    int c;
    memset(dst, 0, sizeof *dst);
    for (c = 0; c < 64; c++) {
        int pc = src->board[c2sq(c)];
        if (pc) dst->board[c2sq(63 - c)] = pc ^ 8;   /* rotate 180 + swap colors */
    }
    dst->side = src->side ^ 1;
    dst->castle = 0;
    dst->ep = -1;
    /* re-locate kings: nn_compute reads ks[] for the mirror flags */
    for (c = 0; c < 64; c++) {
        int pc = dst->board[c2sq(c)];
        if (pc == WK) dst->ks[0] = c2sq(c);
        else if (pc == BK) dst->ks[1] = c2sq(c);
    }
}

/* horizontal mirror (file flip, colors and side unchanged) */
static void hmirror_pos(Pos *dst, const Pos *src) {
    int c;
    memset(dst, 0, sizeof *dst);
    for (c = 0; c < 64; c++) {
        int f = c & 7;
        int mc = (c & 0xF8) | (7 - f);
        int pc = src->board[c2sq(c)];
        if (pc) dst->board[c2sq(mc)] = pc;
    }
    dst->side = src->side;
    dst->castle = 0;
    dst->ep = -1;
    for (c = 0; c < 64; c++) {
        int pc = dst->board[c2sq(c)];
        if (pc == WK) dst->ks[0] = c2sq(c);
        else if (pc == BK) dst->ks[1] = c2sq(c);
    }
}

/* direct per-call cost measurement (`chess nbench`): times nnue_eval and a
   make/undo pair in loops, prints ms per 1000 calls. Convert to cycles with the
   host clock (8088 16 MHz: cyc = ms*16 per call; 286 6 MHz: cyc = ms*6). */
int nnue_bench(void) {
    static Pos pos;
    static unsigned int list[256];
    Undo u;
    int i, n, iters;
    clock_t t0, t1;
    long eval_ms, delta_ms;

    parse_fen(&pos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    nnue_reset(&pos);
    nnue_active = 1;
    n = gen_moves(&pos, list);
    if (n <= 0) return 1;

    iters = 4000;
    t0 = clock();
    for (i = 0; i < iters; i++) nnue_eval(&pos);
    t1 = clock();
    eval_ms = ((long)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;

    iters = 10000;
    {
        unsigned int qm = 0;
        for (i = 0; i < n; i++)
            if (mfl(list[i]) == 0) { qm = list[i]; break; }
        if (!qm) return 1;
        t0 = clock();
#ifdef PROFILE
        {
            long r0 = c_refresh, f0 = c_flip;
#endif
        for (i = 0; i < iters; i++) {
            do_make(&pos, qm, &u);
            undo_move(&pos, qm, &u);
        }
#ifdef PROFILE
            printf("nbench applies=%ld flips=%ld\n", c_refresh - r0, c_flip - f0);
        }
#endif
        t1 = clock();
        delta_ms = ((long)(t1 - t0)) * 1000 / CLOCKS_PER_SEC;
    }

    printf("nbench eval1000=%ld delta1000=%ld\n",
           eval_ms * 1000 / 4000, delta_ms * 1000 / 10000);
    nnue_active = 0;
    return 0;
}

int nnue_selftest(const char *fen) {
    /* static: keeps the 16-bit test's ~2 KB of buffers off the 2048-byte stack */
    static Pos pos, flipped, hm;
    static short a[2][NNUE_N], b[2][NNUE_N];
    static short before[2][NNUE_N], incr[2][NNUE_N], fresh[2][NNUE_N];
    unsigned int *list = movebuf[28];
    int n, i, fail = 0, asym = 0, asymH = 0;

    parse_fen(&pos, fen ? fen : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    if (!nnue_enabled) {   /* deterministic pattern so the test needs no net file */
        long z;
        for (z = 0; z < (long)NNUE_FEATURES * NNUE_N; z++)
            nn_w1[z] = (signed char)((((long)z * 7 + 13) & 255) - 128);
        for (z = 0; z < (long)NNUE_N; z++)
            nn_b1[z] = (signed char)((((long)z * 17 + 3) & 255) - 128);
        for (z = 0; z < (long)NNUE_W2_SIZE; z++)
            nn_w2[z] = (signed char)((((long)z * 11 + 5) & 255) - 128);
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
        int rt = 0;   /* first-failure diagnostic flag */
        for (i = 0; i < n; i++) {
            Undo u;
            memcpy(before, nn_acc, sizeof before);
            do_make(&pos, list[i], &u);
            memcpy(incr, nn_acc, sizeof incr);
            nn_compute(&pos, fresh);
            if (memcmp(incr, fresh, sizeof incr) != 0) {
                if (!rt) {
                    int jj;
                    for (jj = 0; jj < 2 * NNUE_N; jj++)
                        if (((short *)incr)[jj] != ((short *)fresh)[jj]) break;
                    printf("nn rt incr-vs-fresh fail move %d (mv=%04x): acc[%d/%d] incr=%d fresh=%d\n",
                           i, list[i], jj >> 6, jj & 63, ((short *)incr)[jj], ((short *)fresh)[jj]);
                    rt = 1;
                }
                fail++;
            }
            undo_move(&pos, list[i], &u);
            if (memcmp(before, nn_acc, sizeof before) != 0) {
                if (!rt) {
                    int jj;
                    for (jj = 0; jj < 2 * NNUE_N; jj++)
                        if (((short *)before)[jj] != nn_acc[jj >> 6][jj & 63]) break;
                    printf("nn rt undo-fail move %d (mv=%04x): acc[%d/%d] before=%d after=%d\n",
                           i, list[i], jj >> 6, jj & 63, ((short *)before)[jj], nn_acc[jj >> 6][jj & 63]);
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

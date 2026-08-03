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
 * Incremental: do_make() applies per-feature-row deltas to both accumulators
 * and records (row, sign) so undo_move() reverses them exactly. The record is
 * a small near array; the big weight matrix lives in a far segment on 16-bit.
 * A king move that crosses the d/e file boundary flips a POV's mirror flag
 * (every piece re-indexes), so that POV is recomputed from scratch and the
 * delta record is marked for a recompute-on-undo instead.
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

#ifdef PROFILE
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
/* Embed chess.net into the binary (gcc build: OpenBench runs the bare binary, so
   there is no runtime file dependency). Rebuild after re-converting a net. */
__asm__(
    ".section .rodata\n"
    ".global nn_embedded_net_start\n"
    "nn_embedded_net_start:\n"
    ".incbin \"chess.net\"\n"
    ".global nn_embedded_net_end\n"
    "nn_embedded_net_end:\n"
    ".text\n"
);
extern const unsigned char nn_embedded_net_start[];
extern const unsigned char nn_embedded_net_end[];
#endif

static short nn_acc[2][NNUE_N];   /* current pre-activations, white POV / black POV */

/* per-ply record of which (row, sign) deltas were applied, so undo reverses them */
typedef struct { unsigned int row; signed char s; } NnD;
static NnD nn_delta[MAXPLY][2][4];
static int nn_dn[MAXPLY][2];
static int nn_ply;

/* ------------------------------------------------------------------ */
/* feature indexing                                                   */
/* ------------------------------------------------------------------ */

/* horizontal mirror of a compact square (file flips, rank unchanged) */
static int nn_norm(int c, int mirror) {
    if (mirror) c = (c & 0xF8) | (7 - (c & 7));
    return c;
}

/* per-perspective mirror flags from the king squares: mirror a POV's board
   when that POV's king (after its 180 transform for persp 1) sits on e-h,
   so the own king always normalizes to files a-d. */
static void nn_mirrors(Pos *p, int m[2]) {
    m[0] = ((sq2c(p->ks[0]) & 7) >= 4) ? 1 : 0;        /* white king file >= e */
    m[1] = ((7 - (sq2c(p->ks[1]) & 7)) >= 4) ? 1 : 0;  /* R180 of black king file >= e */
}

static int nn_row(int persp, int pc, int sq88, int mirror) {
    int c = sq2c(sq88);
    int ty = TY(pc);
    int r;

    if (persp == 1) {                 /* black POV: 180 flip + color swap */
        pc = CO(pc) ? pc - 8 : pc + 8;
        c = 63 - c;
        ty = TY(pc);
    }
    c = nn_norm(c, mirror);
    if (CO(pc) == 0) {                /* own piece */
        if (ty == 6) return (c >> 3) * 8 + (c & 7);   /* own king: file <= 3 by mirror */
        if (ty == 1) { r = c >> 3; if (r < 1 || r > 6) return -1; return 32 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 80 + (ty - 2) * 64 + c;
        return -1;
    } else {                          /* enemy piece */
        if (ty == 6) return 336 + c;
        if (ty == 1) { r = c >> 3; if (r < 1 || r > 6) return -1; return 400 + (r - 1) * 8 + (c & 7); }
        if (ty >= 2 && ty <= 5) return 448 + (ty - 2) * 64 + c;
        return -1;
    }
}

/* add sign * w1[row] to one perspective's accumulator and record the delta */
static void nn_delta_apply(int persp, int row, int sign) {
    int j;
    if (row < 0) return;
    PCOUNT(c_refresh);
    {
        long base = (long)row * NNUE_N;
        for (j = 0; j < NNUE_N; j++)
            nn_acc[persp][j] += (short)(sign * nn_w1[base + j]);
    }
    if (nn_dn[nn_ply][persp] >= 0 && nn_dn[nn_ply][persp] < 4) {
        nn_delta[nn_ply][persp][nn_dn[nn_ply][persp]].row = (unsigned int)row;
        nn_delta[nn_ply][persp][nn_dn[nn_ply][persp]].s = (signed char)sign;
    }
    nn_dn[nn_ply][persp]++;
}

/* compute one perspective's accumulator from scratch (init with the layer-1 bias) */
static void nn_compute_persp(Pos *p, int persp, short *out) {
    int m[2], sq, k;
    nn_mirrors(p, m);
    for (k = 0; k < NNUE_N; k++) out[k] = nn_b1[k];
    for (sq = 0; sq < 128; sq++) {
        int pc = p->board[sq];
        int row;
        long base;
        if (!pc) continue;
        row = nn_row(persp, pc, sq, m[persp]);
        if (row < 0) continue;
        base = (long)row * NNUE_N;
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
    nn_dn[0][0] = nn_dn[0][1] = 0;
}

/* ------------------------------------------------------------------ */
/* incremental make / undo                                            */
/* ------------------------------------------------------------------ */

void nnue_make(Pos *p, unsigned int m, Undo *u) {
    int from = mfrom(m), to = mto(m), fl = mfl(m);
    int mover_col = p->side ^ 1;          /* side that just moved */
    int mover = p->board[to];             /* mover or promo piece at 'to' */
    int newp = mover;
    int persp, mpost[2], mpre[2], flip[2];

    PCOUNT(c_nn_make);
    if (ispromo(m)) mover = (mover_col == 0) ? WP : BP;   /* it was a pawn */

    /* mirror flags before/after this move (only a king move can change them) */
    nn_mirrors(p, mpost);
    mpre[0] = (mover == WK) ? ((sq2c(from) & 7) >= 4) : mpost[0];
    mpre[1] = (mover == BK) ? ((7 - (sq2c(from) & 7)) >= 4) : mpost[1];
    for (persp = 0; persp < 2; persp++)
        flip[persp] = (mpre[persp] != mpost[persp]) ? 1 : 0;

    nn_dn[nn_ply][0] = nn_dn[nn_ply][1] = 0;

    for (persp = 0; persp < 2; persp++) {
        if (flip[persp]) {
            PCOUNT(c_flip);
            nn_dn[nn_ply][persp] = -1;   /* every piece re-indexed: undo recomputes */
        } else {
            nn_delta_apply(persp, nn_row(persp, mover, from, mpost[persp]), -1);
            if (u->cap != EMPTY && fl != MF_EP)
                nn_delta_apply(persp, nn_row(persp, u->cap, to, mpost[persp]), -1);
            if (fl == MF_EP) {
                int esq = (mover_col == 0) ? to - 16 : to + 16;
                int ep = (mover_col == 0) ? BP : WP;
                nn_delta_apply(persp, nn_row(persp, ep, esq, mpost[persp]), -1);
            }
            if (fl == MF_CASTLE) {
                int rf, rt, rook;
                if (to == 0x06)      { rf = 0x07; rt = 0x05; }
                else if (to == 0x02) { rf = 0x00; rt = 0x03; }
                else if (to == 0x76) { rf = 0x77; rt = 0x75; }
                else                 { rf = 0x70; rt = 0x73; }
                rook = (mover_col == 0) ? WR : BR;
                nn_delta_apply(persp, nn_row(persp, rook, rf, mpost[persp]), -1);
                nn_delta_apply(persp, nn_row(persp, rook, rt, mpost[persp]), +1);
            }
            nn_delta_apply(persp, nn_row(persp, newp, to, mpost[persp]), +1);
        }
    }
    for (persp = 0; persp < 2; persp++)
        if (flip[persp]) nn_compute_persp(p, persp, nn_acc[persp]);
    nn_ply++;
}

void nnue_undo(Pos *p) {
    int persp;
    PCOUNT(c_nn_undo);
    if (nn_ply > 0) nn_ply--;
    for (persp = 0; persp < 2; persp++) {
        int i, j;
        if (nn_dn[nn_ply][persp] < 0) {
            nn_compute_persp(p, persp, nn_acc[persp]);   /* board is pre-make again */
            continue;
        }
        for (i = 0; i < nn_dn[nn_ply][persp]; i++) {
            unsigned int row = nn_delta[nn_ply][persp][i].row;
            int sign = -nn_delta[nn_ply][persp][i].s;
            long base = (long)row * NNUE_N;
            for (j = 0; j < NNUE_N; j++)
                nn_acc[persp][j] += (short)(sign * nn_w1[base + j]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* forward pass                                                       */
/* ------------------------------------------------------------------ */

int nnue_eval(Pos *p) {
    long out = nn_bias;
    int j;
    PCOUNT(c_nn_eval);
    for (j = 0; j < NNUE_N; j++) {
        /* clamp(pre,-1,1) at accumulator quantization 128: the extremes +/-128
           are powers of two, so their terms are shift-only (128*w = w<<7) */
        int a0 = nn_acc[0][j];
        int a1 = nn_acc[1][j];
        int w0 = nn_w2[j];
        int w1_ = nn_w2[NNUE_N + j];

        if (a0 >= 128)       out += (long)(w0 << 7);
        else if (a0 <= -128) out -= (long)(w0 << 7);
        else                 out += (long)(a0 * w0);

        if (a1 >= 128)       out += (long)(w1_ << 7);
        else if (a1 <= -128) out -= (long)(w1_ << 7);
        else                 out += (long)(a1 * w1_);
    }
    {
        int s = (int)(out >> NNUE_SCALE_SHIFT);
        return (p->side == 0) ? s : -s;   /* negamax: side to move */
    }
}

/* ------------------------------------------------------------------ */
/* weight loading                                                      */
/* ------------------------------------------------------------------ */

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
    return 1;
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
int nnue_ensure_loaded(const char *path) {
    if (nnue_load(path)) return 1;
#if !defined(__WATCOMC__)
    return nnue_parse_blob(nn_embedded_net_start,
                           (long)(nn_embedded_net_end - nn_embedded_net_start));
#else
    return 0;
#endif
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
    for (i = 0; i < n; i++) {
        Undo u;
        memcpy(before, nn_acc, sizeof before);
        do_make(&pos, list[i], &u);
        memcpy(incr, nn_acc, sizeof incr);
        nn_compute(&pos, fresh);
        if (memcmp(incr, fresh, sizeof incr) != 0) fail++;
        undo_move(&pos, list[i], &u);
        if (memcmp(before, nn_acc, sizeof before) != 0) fail++;
    }
    nnue_active = 0;

    nnue_reset(&pos);
    printf("nn selftest: moves=%d  acc-sym(R180)=%d  acc-sym(H)=%d  roundtrip-fails=%d  eval=%d\n",
           n, asym, asymH, fail, nnue_eval(&pos));
    return (fail == 0 && asym == 0 && asymH == 0) ? 0 : 1;
}

#ifndef ENGINE_H
#define ENGINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#if defined(__WATCOMC__)
#include <process.h>
#define DBG_PID() ((long)getpid())
#elif defined(_MSC_VER) || defined(__MINGW32__)
#include <process.h>
#define DBG_PID() ((long)_getpid())
#else
#include <unistd.h>
#define DBG_PID() ((long)getpid())
#endif

enum { EMPTY = 0, WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
       BP = 9, BN = 10, BB = 11, BR = 12, BQ = 13, BK = 14 };

#define CO(p) ((p) & 8)      /* 0 = white, 8 = black */
#define TY(p) ((p) & 7)

/* move encoding (16-bit unsigned int)
   bits 0..5   from square (compact rank*8+file, 0..63)
   bits 6..11  to square (compact)
   bits 12..15 flags (MF_*, distinct values compared via mfl())

   flags: 0 = quiet/capture, 1..4 = promote to N/B/R/Q
   (piece type = flag + 1), 5 = en passant, 6 = castling */
#define MF_PROMO_N 1
#define MF_PROMO_B 2
#define MF_PROMO_R 3
#define MF_PROMO_Q 4
#define MF_EP      5
#define MF_CASTLE  6

#define sq2c(s)  (((s) & 7) | (((s) >> 1) & 56))    /* 0x88 sq -> compact 0..63 */
#define c2sq(c)  (((c) & 7) | (((c) & 56) << 1))    /* compact -> 0x88 sq */
#define mfl(m)   (((unsigned)(m) >> 12) & 0xF)
#define mfrom(m) c2sq((unsigned)(m) & 0x3F)
#define mto(m)   c2sq((((unsigned)(m) >> 6) & 0x3F))
#define ispromo(m) (mfl(m) >= MF_PROMO_N && mfl(m) <= MF_PROMO_Q)
#define mpro(m, col) (ispromo(m) ? ((col) | (mfl(m) + 1)) : 0)  /* promo piece, col 0/8 */
#define MK(t, f, fl, pr) ((unsigned int)((sq2c(f) & 0x3F) | \
                          ((sq2c(t) & 0x3F) << 6) | \
                          ((((pr) ? (int)(TY(pr) - 1) : (int)(fl)) & 0xF) << 12)))

typedef struct {
    int board[128];   /* 0x88 board */
    int side;         /* 0 white, 1 black */
    int castle;       /* bit0 WK bit1 WQ bit2 BK bit3 BQ */
    int ep;           /* en-passant target square, or -1 */
    int ks[2];        /* king squares */
} Pos;

typedef struct { int cap, castle, ep; } Undo;

/* staged move generator state (see chess.c). One per active search node. */
typedef struct {
    unsigned int *list;   /* active move list (movebuf[ply]) */
    int n;                /* number of moves in the active list */
    int idx;              /* next index to consider */
    int stage;            /* MG_TT .. MG_DONE */
    unsigned int ttm;     /* transposition-table move (0 = none for now) */
    unsigned int k0, k1;  /* killer moves (0 = none) */
    int caps_only;        /* stop after the captures stage (quiescence) */
} MGen;

#define MAXPLY 32         /* killers[] rows; movebuf rows cover ply 0..MAXPLY-1 */

#define INF  30000
#define MATE 29000

#define TIME_MARGIN_MS 30               /* stop searching this many ms early so the
                                           move is reported before the clock runs out.
                                           Must exceed Windows' clock() granularity
                                           (~15.6 ms) + output overhead or we lose on
                                           time at tight clocks. */

/* ---- shared globals ---- */
extern int g_half, g_full;              /* halfmove clock, fullmove number */
extern unsigned long g_sigs[1024];      /* position signatures for repetition */
extern int g_sigs_n;
extern unsigned int movebuf[32][256];   /* move lists, one row per search ply + aux */
extern volatile int stop_now;
extern long deadline;                   /* ms deadline, 0 = no limit */
extern int post_on;

/* ---- search.c ---- */
void do_make(Pos *p, unsigned int m, Undo *u);
void undo_move(Pos *p, unsigned int m, Undo *u);
int is_attacked(Pos *p, int sq, int by);
int gen_moves(Pos *p, unsigned int *list);
long perft(Pos *p, int depth);
unsigned long pos_sig(Pos *p);
void search_root(Pos *p, int maxdepth);
unsigned int think(Pos *p, int maxdepth);
int bench(int depth);

/* ---- xboard.c ---- */
void dbgf(const char *fmt, ...);
void xb_outf(const char *fmt, ...);
int xboard_main(void);

/* ---- NNUE eval (nnue.c) ----
   One-hot 704-feature net (12 piece types, king file folded to a-d, pawns on
   48 squares), 704 -> 2N -> 1 with the two perspectives sharing one weight
   matrix. Weights are i8 and live far on the 16-bit target (see NNUE.md).
   Quantization (trainer contract): accumulator x128, clamp(pre,-1,1) ->
   [-128,128] with the +/-128 extremes shift-only, w2 x64, bias x8192 in i16;
   score = out >> NNUE_SCALE_SHIFT (5), where the trainer outputs 1.0 = 256 cp. */
#define NNUE_FEATURES    704
#define NNUE_N           64
#define NNUE_SCALE_SHIFT 5    /* out >> 5 = centipawns (trainer: 1.0 net output = 256 cp) */
#define NNUE_W1_SIZE     45056L  /* NNUE_FEATURES * NNUE_N, long so 16-bit ints don't wrap */
#define NNUE_W2_SIZE     128     /* 2 * NNUE_N */
extern int nnue_enabled;    /* a net is loaded */
extern int nnue_active;     /* incremental accumulators are live (during search) */
void nnue_reset(Pos *p);
void nnue_make(Pos *p, unsigned int m, Undo *u);
void nnue_undo(Pos *p);
int nnue_eval(Pos *p);
int nnue_load(const char *path);
int nnue_ensure_loaded(const char *path);
int nnue_selftest(const char *fen);

/* ---- chess.c (board, movegen, eval, perft, FEN) ---- */
void parse_fen(Pos *p, const char *s);
int evaluate(Pos *p);
int gen_caps(Pos *p, unsigned int *list);
int gen_quiets(Pos *p, unsigned int *list);
void mgen_init(Pos *p, MGen *g, int ply, int k0, int k1, unsigned int ttm);
void mgen_init_q(Pos *p, MGen *g, int ply);   /* captures only (quiescence) */
unsigned int next_move(Pos *p, MGen *g);

#endif

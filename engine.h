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

/* Position signature (incremental Zobrist, 64-bit). STORAGE DECISION: a single
   unsigned long long beats four separate 16-bit words on BOTH builds. Verified
   by disassembly: Open Watcom 16-bit already lowers a u64 XOR to four native
   word XORs and u64 == to four short-circuited word cmp/jne - identical to
   hand-split four-16-bit codegen - while gcc keeps a single native 64-bit op.
   So four-16-bit buys nothing on the target and loses on gcc. The collision
   safety of 64 bits (vs the old 32-bit FNV) also lets g_sigs/rep_path store
   the full signature. */
typedef unsigned long long Sig;

typedef struct {
    int board[128];   /* 0x88 board */
    int side;         /* 0 white, 1 black */
    int castle;       /* bit0 WK bit1 WQ bit2 BK bit3 BQ */
    int ep;           /* en-passant target square, or -1 */
    int ks[2];        /* king squares */
    Sig sig;          /* incremental Zobrist signature of this position */
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
#define MAX_G_SIGS 128    /* game-history position signatures. A capture or pawn
                             move irreversibly changes the position, so history
                             BEFORE the last zeroing move can never be a
                             threefold partner; apply_move clears it there. And
                             the 50-move rule (MAX_HALF=100) bounds the quiet
                             stretch after a zeroing move, so 128 entries is
                             provably enough (1024 was 8 KB of far data). */
#define MAX_REP_PATH 64   /* search-line repetition signatures */
#define MAX_HALF 100      /* half-moves before the 50-move rule declares a draw */

/* 16-bit evaluation score. All eval/search scores (alpha/beta/best/root
   scores) live in this type so the gcc (32-bit int) build does the SAME i16
   arithmetic as the 16-bit target (where int is already 16-bit), keeping the
   two builds deterministic by construction. INF/MATE fit: 29000/30000 < 32767. */
typedef short Score;

#define INF  30000
#define MATE 29000

#define TIME_MARGIN_MS 30               /* stop searching this many ms early so the
                                           move is reported before the clock runs out.
                                           Must exceed Windows' clock() granularity
                                           (~15.6 ms) + output overhead or we lose on
                                           time at tight clocks. */

/* ---- profiling / vclock call counters ----
   Compiled in with -DPROFILE (`make profile`, build.ps1 -Profile) or -DVCLOCK
   (the gcc build, where the virtual clock charges the weighted model). Normal
   16-bit builds expand PCOUNT to nothing, so the counters cost zero cycles. */
#if defined(PROFILE) || defined(VCLOCK)
#define PCOUNT(c) (c)++
extern long c_anodes;        /* alphabeta entry */
extern long c_qnodes;        /* qsearch entry */
extern long c_nextmove;      /* next_move entry */
extern long c_make;          /* do_make entry */
extern long c_undo;          /* undo_move entry */
extern long c_gen_moves;     /* gen_moves entry */
extern long c_gen_caps;      /* gen_caps entry */
extern long c_gen_quiets;    /* gen_quiets entry */
extern long c_nn_make;       /* nnue_make entry */
extern long c_nn_undo;       /* nnue_undo entry */
extern long c_nn_eval;       /* nnue_eval entry */
extern long c_refresh;       /* feature-row deltas applied (nn_delta_apply) */
extern long c_flip;          /* mirror-flip recompute paths (nnue_make) */
extern long c_isattacked;    /* is_attacked entry */
extern long c_possig;        /* pos_sig entry */
extern long c_tt_probe;      /* transposition-table probe entry */
extern long c_tt_store;      /* transposition-table store entry */
#ifdef PROFILE
int profile(int depth);
#endif
#else
#define PCOUNT(c) ((void)0)
#endif

/* ---- shared globals ---- */
extern int g_half, g_full;              /* halfmove clock, fullmove number */
extern Sig g_sigs[MAX_G_SIGS];          /* position signatures for repetition */
extern int g_sigs_n;
extern unsigned int movebuf[32][256];   /* move lists, one row per search ply + aux */
extern volatile int stop_now;
extern long deadline;                   /* ms deadline, 0 = no limit */
extern int post_on;

/* ---- search.c ---- */
void do_make(Pos *p, unsigned int m, Undo *u);
void undo_move(Pos *p, unsigned int m, Undo *u);
int is_attacked(Pos *p, int sq, int by);
int sq_on_king_line(Pos *p, int sq, int s);
int gen_moves(Pos *p, unsigned int *list);
long perft(Pos *p, int depth);
Sig pos_sig(Pos *p);
void search_root(Pos *p, int maxdepth);
unsigned int think(Pos *p, int maxdepth);
int bench(int depth);
int profile(int depth);
int sbench(void);

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
Score nnue_eval(Pos *p);
int nnue_load(const char *path);
int nnue_ensure_loaded(const char *path);
int nnue_ensure_default(void);
void nnue_tables_init(void);   /* one-time table builds (rowtab/fwd/castle) after net load */
int nnue_selftest(const char *fen);
int nnue_bench(void);

/* ---- transposition table (tt.c) ----
   One far 64 KB table on the 16-bit target (4096 x 16-byte entries), plain
   array on gcc. Probe/store keyed on the full Pos.sig; see tt.c. */
enum { TT_EXACT = 0, TT_LOWER = 1, TT_UPPER = 2 };  /* stored score bound */
void tt_clear(void);                                    /* empty the table */
int  tt_probe(Pos *p, int ply, unsigned int *move_out, Score *score_out,
              int *flag_out, int *depth_out);           /* 1 = hit */
void tt_store(Pos *p, unsigned int move, int depth, Score score, int flag, int ply);

/* ---- virtual time clock (vclock.c) ----
   VirtualTime=1 makes the engine ignore the GUI's time commands and pace
   itself against a 40/2h + 20/1h repeating control using a per-CPU cycle
   model (CPU_model + CPU_KHz). See time-control-and-testing-methodology.md.
   The 32-bit (VCLOCK) build charges the search's sub-functions at measured
   per-call costs; the 16-bit build keeps a scalar cycles/node. */
enum { VCPU_80286 = 0, VCPU_8088, VCPU_8086 };
extern int vtime_mode;       /* 1 = virtual clock active (ignore GUI time) */
extern long vcpu_khz;        /* CPU clock in KHz (cycles per ms); long: 50000 KHz overflows a 16-bit int */
extern int vcpu_model;       /* VCPU_* */
extern long vtotal_nodes;    /* nodes searched so far in the current move */
extern long vmax_nodes;      /* scalar node cap for the current move (0 = none) */
void vclock_set_model(const char *name);
void vclock_set_khz(long khz);
void vclock_set_enabled(const char *val);
void vclock_newgame(void);              /* reset the period clock for a new game */
void vclock_reset(void);                /* reset per-move state + counters before a move */
long vclock_budget_ms(void);            /* per-move virtual budget in ms (refills periods) */
void vclock_set_budget(long budget_ms); /* set the move's stop condition from its budget */
int vclock_budget_hit(void);            /* 1 = the move's virtual budget is consumed */
long vclock_charge(void);               /* deduct the move's consumed time; returns consumed ms */
#ifdef VCLOCK
long vclock_est_nps(long nodes);        /* weighted-model NPS for the modeled CPU (bench output) */
#endif

/* ---- chess.c (board, movegen, eval, perft, FEN) ---- */
void parse_fen(Pos *p, const char *s);
Score evaluate(Pos *p);
void zob_init(void);              /* one-time Zobrist key tables (dedicated init) */
int gen_caps(Pos *p, unsigned int *list);
int gen_quiets(Pos *p, unsigned int *list);
void mgen_init(Pos *p, MGen *g, int ply, int k0, int k1, unsigned int ttm);
void mgen_init_q(Pos *p, MGen *g, int ply);   /* captures only (quiescence) */
unsigned int next_move(Pos *p, MGen *g);

#endif

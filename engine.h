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
extern unsigned int movebuf[12][256];   /* per-depth move lists + root + aux */
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

/* ---- xboard.c ---- */
void dbgf(const char *fmt, ...);
void xb_outf(const char *fmt, ...);
int xboard_main(void);

/* ---- chess.c ---- */
void parse_fen(Pos *p, const char *s);

#endif

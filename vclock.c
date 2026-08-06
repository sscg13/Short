/* vclock.c - virtual time clock for the 40/2h + 20/1h repeating control

   When VirtualTime=1 the engine ignores the GUI's time commands (`time`,
   `otim`, `st`, `level`) and paces itself as if it were a real CPU_model @
   CPU_KHz. Each move is granted a virtual time budget (180 s/move on average,
   time banked within the current period, hard flag at move 40 and every 20
   after that), and the search is stopped once the estimated cost of the work
   it has done reaches that budget. See time-control-and-testing-methodology.md
   sections 2, 5 and 7.

   Cost model:
     - 16-bit build (no VCLOCK): a scalar cycles/node per CPU model, NNUE vs
       material, fitted to the measured bench totals.
     - 32-bit build (VCLOCK, the OpenBench/testing build): a WEIGHTED model
       that charges the search's sub-functions at their measured per-call
       costs, so a move's cost tracks how the work is actually spent: qsearch
       rate, move density (branching factor), NNUE activity, and threat probes
       all move the estimate instead of one flat per-node number. Weights come
       from the emulator sbench/nbench measurements; the per-node base R is
       fitted so the model reproduces the measured bench totals exactly.

   Calibration provenance (86Box interpreter measurements, NNUE_OPTIMIZATION.md).
   COPY-MAKE re-fit (2026-08, branch copy-make-nnue): the accumulator stack made
   make/undo ~2x cheaper (undo is one 256-byte copy, no delta reversal), so the
   profile-1 totals dropped to 1.859e9 (8088 @16 MHz) / 0.617e9 (80286 @6 MHz)
   at the SAME per-call costs (sbench re-verified on the AMI-clone machine, ~1%).
   All per-call weights (search, ev, rf) are therefore UNCHANGED; only the
   per-node base R re-fits to the new totals (the savings were absorbed there).

   SEARCH-OPT re-fit (2026-08, branch `optimization`): the legality-check skip
   (a non-king, non-EP move from a square off the mover king's lines is legal
   when not in check) cut the profile-1 is_attacked calls 34,973 -> 28,017. Per
   call costs are UNCHANGED (the same sbench numbers re-verified, ~1%); the
   weighted model auto-tracks the count drop, and only the per-node base R and
   the scalar cycles/node re-fit to the new profile totals: 1.814e9 (8088) /
   0.607e9 (80286) for bench 1 (13230 nodes). The MVV-LVA score table (8x8,
   next_move) and the MG_TT stage skip are inside the `gm` (drain) weight, which
   re-measured within noise (49.7K vs 48.5K c286).

   MOVE-SWEEP re-fit (same branch): gen_caps/gen_quiets now sweep only the 64
   on-board 0x88 squares (the other 64 entries of board[128] are always EMPTY),
   which cut the per-call costs to gc 16.5K / gq 18.7K / drain 48.2K c286
   (was 17.3K / 20.0K / 48.5K; 8088 scaled proportionally). Profile-1 total
   -> 1.808e9 (8088) / 0.604e9 (80286). R and the scalar cycles/node re-fit
   again.

      per-call cycles       8088      80286
        is_attacked          6512       2262
        pos_sig             33488       9678
        gen_caps            51248      17298
        gen_quiets          60416      20046
        gen_moves           146448     48468     (full staged drain, sbench)
        make or undo         1560        525     (pair 3120/1050)
        NNUE apply elem      5613       1980     (nbench, 12 applies/capture pair)
        nnue_eval fwd       19328       6588
      bench 1 totals (13230 nodes): 2.354e9 / 0.815e9 (pre-copy-make),
      1.859e9 / 0.617e9 (copy-make), then 1.814e9 / 0.607e9 (search-opt) and
      1.808e9 / 0.604e9 (move-sweep). Material build (10152 nodes) unchanged
      at 0.689e9 / 0.233e9.
      R (per-node base) is what those terms leave over, fitted per NNUE state.
      8086 = 16-bit-bus 8088 core, an ESTIMATE (re-derive before trusting). */

#include "engine.h"

int vtime_mode = 0;          /* VirtualTime=1: use the virtual clock */
long vcpu_khz = 25000;       /* CPU_KHz: cycles per ms of the modeled CPU */
int vcpu_model = VCPU_80286; /* CPU_model index */

long vtotal_nodes = 0;       /* nodes searched so far in the current move */
long vmax_nodes = 0;         /* scalar node cap for the current move (0 = none) */

/* virtual period state: 40 moves in 2 h, then 20 moves per 1 h, repeating */
static long vperiod_ms;      /* virtual ms left in the current period */
static int  vperiod_left;    /* moves left in the current period */
static int  vperiod_started; /* first period not yet granted */

/* scalar cycles/node, NNUE / material (16-bit build) */
#ifndef VCLOCK
static const long cpn_tab[3][2] = {
    { 45684L,  22980L },   /* VCPU_80286  (move-sweep re-fit) */
    { 137113L, 67800L },   /* VCPU_8088   (move-sweep re-fit) */
    { 93000L,  45000L },   /* VCPU_8086 (estimate) */
};
#endif

#ifdef VCLOCK
static long long vbudget_cyc;   /* weighted cycle budget for the current move */

typedef struct { long att, ps, gc, gq, gm, mk, nm, rf, ev, rn, rm; } VW;
static const VW vw_tab[3] = {
    /*   att    ps     gc     gq      gm   mk   nm   rf    ev     rn     rm */
    {  2262,  9678, 16476, 18672, 48192, 525, 1000, 1980,  6588,  4018,  6400 }, /* 80286 */
    {  6512, 33488, 49188, 55929, 141970, 1560, 3000, 5613, 19328, 16008, 19120 }, /* 8088 */
    {  5000, 24000, 38000, 45000, 110000, 1100, 2000, 4000, 14000, 30000, 14000 }, /* 8086 est */
};

static long long vclock_cyc(void) {
    const VW *w = &vw_tab[vcpu_model];
    long long r = (long long)(nnue_enabled ? w->rn : w->rm) * (c_anodes + c_qnodes);
    r += (long long)w->att * c_isattacked;
    r += (long long)w->ps  * c_possig;
    r += (long long)w->gc  * c_gen_caps;
    r += (long long)w->gq  * c_gen_quiets;
    r += (long long)w->gm  * c_gen_moves;
    r += (long long)w->mk  * (c_make + c_undo);
    r += (long long)w->nm  * (c_nn_make + c_nn_undo);
    r += (long long)w->rf  * c_refresh;
    r += (long long)w->ev  * c_nn_eval;
    return r;
}

/* NPS the weighted model predicts for the modeled CPU (vclock_cyc over the
   accumulated counters, converted at vcpu_khz). Used by `bench` so OpenBench's
   nps reflects the target 286 @ 25 MHz, not the host. */
long vclock_est_nps(long nodes) {
    long long cyc = vclock_cyc();
    if (cyc <= 0 || vcpu_khz <= 0) return 0;
    return (long)((long long)nodes * vcpu_khz * 1000LL / cyc);
}
#endif

void vclock_set_model(const char *name) {
    if (!name) return;
    if (strcmp(name, "8088") == 0) vcpu_model = VCPU_8088;
    else if (strcmp(name, "8086") == 0) vcpu_model = VCPU_8086;
    else vcpu_model = VCPU_80286;   /* "80286" or anything else */
}

void vclock_set_khz(long khz) {
    if (khz < 100) khz = 100;         /* keep the /100 scalings in range */
    if (khz > 50000) khz = 50000;
    vcpu_khz = khz;
}

void vclock_set_enabled(const char *val) {
    if (!val) return;
    vtime_mode = (val[0] == '1') || !strcmp(val, "true") || !strcmp(val, "True")
              || !strcmp(val, "yes") || !strcmp(val, "on");
}

void vclock_newgame(void) {
    vperiod_started = 0;
    vclock_reset();
}

/* zero the per-move state (and, on VCLOCK builds, the weighted counters) */
void vclock_reset(void) {
    vtotal_nodes = 0;
    vmax_nodes = 0;
#ifdef VCLOCK
    vbudget_cyc = 0;
    c_anodes = c_qnodes = c_nextmove = 0;
    c_make = c_undo = c_gen_moves = c_gen_caps = c_gen_quiets = 0;
    c_nn_make = c_nn_undo = c_nn_eval = c_refresh = c_flip = 0;
    c_isattacked = 0;
    c_possig = 0;
#endif
}

/* per-move virtual budget in ms; refills the current period when it is spent.
   The first period is 40 moves in 2 h, every later period 20 moves in 1 h, so
   the per-move average is 180 s throughout. */
long vclock_budget_ms(void) {
    if (!vperiod_started || vperiod_left <= 0) {
        if (!vperiod_started) {               /* period 1: 40 moves in 2 h */
            vperiod_ms = 7200L * 1000L;
            vperiod_left = 40;
            vperiod_started = 1;
        } else {                              /* repeating: 20 moves in 1 h */
            vperiod_ms = 3600L * 1000L;
            vperiod_left = 20;
        }
    }
    return vperiod_ms / vperiod_left;
}

/* set the stop condition for the current move from its virtual budget */
void vclock_set_budget(long budget_ms) {
#ifdef VCLOCK
    vbudget_cyc = (long long)budget_ms * vcpu_khz;
#else
    long cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1];
    if (budget_ms <= 0 || cpn < 100) vmax_nodes = 0;
    else vmax_nodes = (long)(((unsigned long)budget_ms / 100) * (unsigned long)vcpu_khz
                             / ((unsigned long)cpn / 100));
#endif
}

/* 1 when the current move has consumed its virtual budget */
int vclock_budget_hit(void) {
#ifdef VCLOCK
    return vbudget_cyc > 0 && vclock_cyc() >= vbudget_cyc;
#else
    return vmax_nodes > 0 && vtotal_nodes >= vmax_nodes;
#endif
}

/* charge the current period the virtual ms the completed move consumed and
   close out its bookkeeping; returns the consumed virtual ms. */
long vclock_charge(void) {
    long vms;
#ifdef VCLOCK
    vms = (long)(vclock_cyc() / vcpu_khz);
#else
    long cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1];
    if (vtotal_nodes > 0 && cpn >= 100 && vcpu_khz >= 100)
        vms = (long)(((unsigned long)vtotal_nodes / 100) * (unsigned long)cpn
                     / ((unsigned long)vcpu_khz / 100));
    else vms = 0;
#endif
    vperiod_ms -= vms;
    if (vperiod_ms < 0) vperiod_ms = 0;
    if (vperiod_left > 0) vperiod_left--;
    vtotal_nodes = 0;
    vmax_nodes = 0;
#ifdef VCLOCK
    vbudget_cyc = 0;
#endif
    return vms;
}

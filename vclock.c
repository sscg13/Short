/* vclock.c - virtual time clock for the 40/2h + 20/1h repeating control

   When VirtualTime=1 the engine ignores the GUI's time commands (`time`,
   `otim`, `st`, `level`) and paces itself as if it were a real CPU_model @
   CPU_KHz. Each move is granted a virtual time budget (180 s/move on average,
   time banked within the current period, hard flag at move 40 and every 20
   after that), and the search is stopped once the estimated cost of the work
   it has done reaches that budget. See TESTING.md
   sections 2, 5 and 7.

   Cost model:
     - 16-bit build (no VCLOCK): a scalar cycles/node per CPU model, NNUE vs
       material, fitted to the measured bench totals.
     - gcc build (VCLOCK, the OpenBench/testing build): a WEIGHTED model
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

   BATCHED-APPLY re-fit (same branch): nnue_make now applies +w1[to]-w1[from]
   (normal) or +w1[to]-w1[from]-w1[cap] (capture/EP) in ONE 64-element pass
   with a single word-RMW per element (nn_make_move_/nn_make_cap_ asm, or the
   matching scalar C). The per-call `rf` weight becomes the batched-apply cost
   and `c_refresh` now counts BATCH calls (44,645 in profile-1, was 114,921
   single-row applies). Castling deltas are precomputed. The nbench make+undo
   pair dropped 24,810 -> 10,710 c286 (-57%), and the profile-1 total -> 0.544e9
   (80286, measured 90.68 s @6 MHz) / ~1.44e9 (8088, estimated by the same
   apply cut). rn and rf re-fit: rf 1980 -> 2400 (286), 5613 -> 6800 (8088);
   rn 4018 -> 8561 (286), 16008 -> 13549 (8088); scalar cpn_tab -> 41126 (286)
   / 108844 (8088 est).

   ZOBRIST re-fit (branch `optimization`): pos_sig is now O(1) - an incremental
   Zobrist signature maintained by make/undo (was a 128-square FNV-1a at 9,678
   c286 per call). The cost moved INTO make/undo (a few u64 XORs each, absorbed
   in `mk`), so `ps` drops to ~0 and `rn` re-fits to the new profile total.

   TT re-fit (2026-08, branch `tt`): a 64 KB far transposition table (tt.c)
   shrinks the tree (bench totals: 8088 NNUE profile-1 1.809e9 cyc, 286 0.592e9)
   and adds probe/store calls (tp/ts). The full weight table was RE-MEASURED on
   the emulators (sbench, uninstrumented build): mk was ~4x stale and ps ~2.5x
   stale (the incremental-Zobrist cost in make/undo was never re-fit into them),
   so rn/rm re-fit to the shipped bench-1 totals as well. nm/rf/ev still carry
   the older nbench numbers; re-verify before trusting deep-search extrapolation.

      per-call cycles       8088      80286
        is_attacked          6512       2262
        pos_sig               ~100        ~40     (field read, was 33488/9678 FNV)
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
      8086 = 16-bit-bus 8088 core, an ESTIMATE (re-derive before trusting).

   QUIET-HISTORY re-fit (2026-08, branch `quiet-history`): MG_QUIETS now orders
   the generated quiets by a piece-to quiet-history table (qhist[2][6][64], see
   search.c) with a selection-sort exactly like the MVV-LVA caps stage. The new
   cost is inside next_move (no per-call weight; same treatment as MVV-LVA), so
   it lands in the per-node base R and the scalar cpn_tab. Re-measured bench-1
   totals on the emulators: 1.936e9 (8088) / 0.6215e9 (80286) for the SAME 13,230
   nodes (depth-1 node count is ordering-independent - no beta cutoffs fire at
   the root, so the counts verify the re-fit is purely the added selection cost).
   Delta = +9,686 cyc/node (8088) / +1,326 (80286); the material build gets the
   same per-node delta (the selection loop never touches NNUE). 8086 est = 0.75x
   of the 8088 delta (matches the existing 8086/8088 weight ratio).

   PVS + FAIL-SOFT re-fit (2026-08, branch `pvs`): alphabeta now searches the
   first legal move at full window and the rest at a zero window (re-searching
   only on a fail high), and qsearch returns the best score found (fail-soft)
   instead of clamping to alpha. Bench-1 node count rose 13,230 -> 13,458 (root
   zero-window re-searches) but the per-node cost DROPPED (zero-window cutoffs
   prune more than the re-searches add): re-measured bench-1 totals 1.950e9
   (8088) / 0.6255e9 (80286) at 13,458 nodes. Delta = -1,422 cyc/node (8088) /
   -504 (80286), applied to R and the scalar cpn_tab; the material build gets
   the same per-node delta (the search-structure change is NNUE-independent).
   8086 est = 0.75x of the 8088 delta. Bench 4 = 653,046 (was 880,565). */

#include "engine.h"

i16 vtime_mode = 0;          /* VirtualTime=1: use the virtual clock */
i32 vcpu_khz = 25000;       /* CPU_KHz: cycles per ms of the modeled CPU */
i16 vcpu_model = VCPU_80286; /* CPU_model index */

i32 vtotal_nodes = 0;       /* nodes searched so far in the current move */
i32 vmax_nodes = 0;         /* scalar node cap for the current move (0 = none) */

/* virtual period state: 40 moves in 2 h, then 20 moves per 1 h, repeating */
static i32 vperiod_ms;      /* virtual ms left in the current period */
static i16 vperiod_left;    /* moves left in the current period */
static i16 vperiod_started; /* first period not yet granted */

/* scalar cycles/node, NNUE / material (16-bit build) */
#ifndef VCLOCK
static const i32 cpn_tab[3][2] = {
    { 41948L,  23802L },   /* VCPU_80286  (PVS re-fit: -504 cyc/node) */
    { 117108L, 76064L },   /* VCPU_8088   (PVS re-fit: -1422 cyc/node) */
    { 91198L,  51198L },   /* VCPU_8086 (estimate) */
};
#endif

#ifdef VCLOCK
static i64 vbudget_cyc;   /* weighted cycle budget for the current move */

typedef struct { i32 att, ps, gc, gq, gm, mk, nm, rf, ev, rn, rm, tp, ts; } VW;
static const VW vw_tab[3] = {
    /*   att    ps     gc     gq      gm    mk    nm   rf    ev     rn      rm      tp    ts */
    {  2316,   102, 16890, 18258, 46818, 1956, 1000, 2400,  6588,   8040,   8723,   594,  492 }, /* 80286 */
    {  6368,   272, 46880, 54912, 137680, 6144, 3000, 6800, 19328,  36185,  31934,  1968, 1808 }, /* 8088 */
    {  4776,   204, 35160, 41184, 103260, 4608, 2250, 5100, 14496,  27139,  23951,  1476, 1356 }, /* 8086 est */
};
/* Re-fit (2026-08, TT): att/gc/gq/gm/mk/ps/tp/ts are fresh sbench measurements
   on the uninstrumented 16-bit build (8088 @16 / 286 @6 MHz). mk and ps were
   ~4x / ~2.5x STALE (the incremental-Zobrist cost folded into make/undo was
   never re-fit). nm/rf/ev kept from the nbench measurements (re-verify: the
   rn/rm per-CPU ratio is not fully consistent). rn/rm re-fit to the SHIPPED
   (no-counter) bench-1 totals, so the model predicts the real engine's NPS.
   Re-fit (2026-08, quiet-history): the MG_QUIETS selection-sort cost lands in
   the per-node base, so rn/rm += the measured delta (+1326 c286 / +9686 c88);
   the material base rm gets the same delta (the sort never touches NNUE).
   Re-fit (2026-08, PVS): the zero-window search + fail-soft qsearch changes the
   node count AND the per-node cost; rn/rm += the measured bench-1 delta
   (-504 c286 / -1422 c88) so the model reproduces the new totals. */

static i64 vclock_cyc(void) {
    const VW *w = &vw_tab[vcpu_model];
    i64 r = (i64)(nnue_enabled ? w->rn : w->rm) * (c_anodes + c_qnodes);
    r += (i64)w->att * c_isattacked;
    r += (i64)w->ps  * c_possig;
    r += (i64)w->gc  * c_gen_caps;
    r += (i64)w->gq  * c_gen_quiets;
    r += (i64)w->gm  * c_gen_moves;
    r += (i64)w->mk  * (c_make + c_undo);
    r += (i64)w->nm  * (c_nn_make + c_nn_undo);
    r += (i64)w->rf  * c_refresh;
    r += (i64)w->ev  * c_nn_eval;
    r += (i64)w->tp  * c_tt_probe;
    r += (i64)w->ts  * c_tt_store;
    return r;
}

/* NPS the weighted model predicts for the modeled CPU (vclock_cyc over the
   accumulated counters, converted at vcpu_khz). Used by `bench` so OpenBench's
   nps reflects the target 286 @ 25 MHz, not the host. */
i32 vclock_est_nps(i32 nodes) {
    i64 cyc = vclock_cyc();
    if (cyc <= 0 || vcpu_khz <= 0) return 0;
    return (i32)((i64)nodes * vcpu_khz * 1000LL / cyc);
}
#endif

void vclock_set_model(const char *name) {
    if (!name) return;
    if (strcmp(name, "8088") == 0) vcpu_model = VCPU_8088;
    else if (strcmp(name, "8086") == 0) vcpu_model = VCPU_8086;
    else vcpu_model = VCPU_80286;   /* "80286" or anything else */
}

void vclock_set_khz(i32 khz) {
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
    c_tt_probe = 0;
    c_tt_store = 0;
#endif
}

/* per-move virtual budget in ms; refills the current period when it is spent.
   The first period is 40 moves in 2 h, every later period 20 moves in 1 h, so
   the per-move average is 180 s throughout. */
i32 vclock_budget_ms(void) {
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
void vclock_set_budget(i32 budget_ms) {
#ifdef VCLOCK
    vbudget_cyc = (i64)budget_ms * vcpu_khz;
#else
    i32 cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1];
    if (budget_ms <= 0 || cpn < 100) vmax_nodes = 0;
    else vmax_nodes = (i32)(((u32)budget_ms / 100) * (u32)vcpu_khz
                            / ((u32)cpn / 100));
#endif
}

/* 1 when the current move has consumed its virtual budget */
i16 vclock_budget_hit(void) {
#ifdef VCLOCK
    return vbudget_cyc > 0 && vclock_cyc() >= vbudget_cyc;
#else
    return vmax_nodes > 0 && vtotal_nodes >= vmax_nodes;
#endif
}

/* charge the current period the virtual ms the completed move consumed and
   close out its bookkeeping; returns the consumed virtual ms. */
i32 vclock_charge(void) {
    i32 vms;
#ifdef VCLOCK
    vms = (i32)(vclock_cyc() / vcpu_khz);
#else
    i32 cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1];
    if (vtotal_nodes > 0 && cpn >= 100 && vcpu_khz >= 100)
        vms = (i32)(((u32)vtotal_nodes / 100) * (u32)cpn
                    / ((u32)vcpu_khz / 100));
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

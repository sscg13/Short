/* vclock.c - virtual time for the 40/2h + 20/1h repeating control

   VirtualTime=1 budgets work for CPU_model at CPU_KHz, ignoring GUI time
   commands. Each period replaces the remaining bank: 40 moves in 2 hours,
   then 20 moves in 1 hour, repeating. Soft/hard limits are half/1.5 times
   the average allocation, except that the last move may use the bank.

   DOS uses scalar cycles/node for NNUE or material. Native VCLOCK builds
   charge measured operation costs plus fitted per-node residuals; cached
   TT eval writes and repetition work are included. See TESTING.md.

   Recalibrated 2026-10-08 after the Watcom attack/SEE, quiet-history, NNUE
   indexing optimizations. Original main search and TT are unchanged.
   Unprofiled -0 -ml -ox measurements:
   86Box interpreter 80286 @6 MHz, 8088 @16 MHz, 8086 @8 MHz; MAME Nimbus
   80186 @8 MHz processor totalcycles. Native PROFILE supplies matching
   counters and all eight position results with the default v5 net 56329CCE.

   Unchanged board/NNUE/SEE primitives retain verified sbench/nbench costs.
   Whole searches and original public TT paths are remeasured; compiled
   kernel checks guard reuse. Only rn/rm fit whole NNUE/material depths
   2 and 4, with equal relative-error least squares; depth 3 is held out.
   Full setup/output windows and counters remain in the fit. Fixed benchmark
   overhead and varying move-picker work mean one residual does not fit all
   depths exactly. The DOS scalar rows anchor measured depth-4 cycles/node;
   they remain coarse approximations of different search workloads.

   Dated inputs, hashes, fit/holdout errors and reproduction:
   artifacts/nonfunctional-main-2026-10-08/REPORT.md and model-audit/proposal.json.
   Emulator calibration does not establish absolute physical-hardware time.
*/

#include "engine.h"

i16 vtime_mode = 0;          /* VirtualTime=1: use the virtual clock */
i32 vcpu_khz = 25000;       /* CPU_KHz: cycles per ms of the modeled CPU */
i16 vcpu_model = VCPU_80286; /* CPU_model index */

i32 vtotal_nodes = 0;       /* nodes searched so far in the current move */
i32 vmax_nodes = 0;         /* scalar hard node cap (0 = none) */
#ifndef VCLOCK
static i32 vsoft_nodes;
#endif

/* virtual period state: 40 moves in 2 h, then 20 moves per 1 h, repeating */
static i32 vperiod_ms;      /* virtual ms left in the current period */
static i16 vperiod_left;    /* moves left in the current period */
static i16 vperiod_started; /* first period not yet granted */

/* scalar cycles/node, NNUE / material (16-bit build) */
#ifndef VCLOCK
static const i32 cpn_tab[4][2] = {
    { 41874L, 28465L }, /* 80286: measured depth-4 NNUE / material */
    { 131689L, 87017L }, /* 8088: measured depth-4 NNUE / material */
    { 114000L, 74696L }, /* 8086: measured depth-4 NNUE / material */
    { 63915L, 43351L }, /* 80186: measured depth-4 NNUE / material */
};
#endif

#ifdef VCLOCK
static i64 vbudget_cyc, vsoft_cyc; /* weighted hard/soft limits */

typedef struct { i32 att, ps, gc, gq, gm, mk, nm, rf, ev, rn, rm, tp, ts; } VW;
static const VW vw_tab[4] = {
    /* att ps gc gq gm mk nm rf ev rn rm tp ts: cycles per counted operation */
    {   1538,      0,  14282,  15518, 145552,   1019,    915,   3147,   5109,  11300,  10800,    700,    700 }, /* 80286 */
    {   4505,      0,  41360,  47233, 486653,   3268,   2944,  10348,  15596,  36000,  30700,   2350,   2500 }, /* 8088 */
    {   3863,      0,  35340,  40823, 431533,   2856,   2527,   9535,  13072,  29900,  25800,   2000,   2100 }, /* 8086 */
    {   2320,    139,  21355,  23754, 227932,   1596,   1463,   5059,   7540,  15000,  14700,   1250,   1300 }, /* 80186 */
};
/* nm: average quiet/capture plan pair minus its matched board pair, /2.
   rf: mean quiet/capture (materialized pair - matched plan pair), /2 perspectives.
   Each pair materializes two perspective batches; undo does not materialize.
   Quiet/capture and mirror-flip differences remain approximations in rn/rm.
   tp rounds the slowest measured probe up to 50 cycles. ts similarly covers
   the maximum original public result/eval path, including reused eval and
   mates. Result-store samples include the public key-fold overhead.
   86Box ps remains zero and absorbed in rn/rm: these searches have no pos_sig
   calls, and isolated loops include overhead. Nimbus uses its exact marker
   interval. Repetition terms below are retained
   from their earlier measured/scaled calibration because that code is unchanged. */

/* Upcoming twofold: fixed overhead measured on the identical 10037-node
   bench-1 tree. Revised 286 scans measured over 20000 isolated calls:
   125 cycles/history comparison, 107/upcoming-history entry. The 956-cycle
   wrong-side lookup sample rounds up to 1000. Successful move checks also
   charge existing make/undo, NNUE-plan and attack counters. Other CPUs'
   variable terms are estimates scaled by measured fixed-overhead ratios.
   Full comparisons are charged conservatively, including ones the old
   alpha-beta repetition check already performed. See REPETITION_EXPERIMENT.md. */
typedef struct { i32 fixed, scan, upscan, lookup; } RepCost;
static const RepCost rep_cost[4] = {
    { 657, 125, 107, 1000 }, /* 286 */
    { 4114, 783, 670, 6262 }, /* 8088 */
    { 3284, 625, 535, 4998 }, /* 8086 */
    { 1129, 215, 184, 1718 }, /* 80186 */
};

/* SEE: verified unchanged uninstrumented 4000-call fixtures on all CPUs, with
   native PROFILE call/scan/attack counts. Entry rounds up to 50 cycles.
   Remove entry and measured attack costs from each fixture; round the slowest
   scan residual up to 1000 cycles with at least 20% headroom. The full rate
   is charged even when a pawn/knight scan exits early. Reproduce with
   -DSEE_TEST and `chess seebench`; rn/rm also undergo the whole-search fit. */
typedef struct { i32 entry, step; } SeeCost;
static const SeeCost see_cost[4] = {
    { 450, 3000 }, /* 80286: measured entry / conservative scan */
    { 1350, 9000 }, /* 8088: measured entry / conservative scan */
    { 1100, 8000 }, /* 8086: measured entry / conservative scan */
    { 600, 5000 }, /* 80186: measured entry / conservative scan */
};

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
    /* Eager eval-only writes reuse the probed slot; conservatively charge the
       shared result/eval path cost even for a protected collision that exits early. */
    r += (i64)w->ts  * c_tt_eval;
    r += (i64)rep_cost[vcpu_model].fixed * (c_anodes + c_qnodes);
    r += (i64)rep_cost[vcpu_model].scan * c_rep_scan;
    r += (i64)rep_cost[vcpu_model].upscan * c_rep_upscan;
    r += (i64)rep_cost[vcpu_model].lookup * c_rep_lookup;
    r += (i64)see_cost[vcpu_model].entry * c_see;
    r += (i64)see_cost[vcpu_model].step * c_see_step;
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
    else if (strcmp(name, "80186") == 0) vcpu_model = VCPU_80186;
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
#ifndef VCLOCK
    vsoft_nodes = 0;
#endif
#ifdef VCLOCK
    vbudget_cyc = vsoft_cyc = 0;
    c_anodes = c_qnodes = c_nextmove = 0;
    c_make = c_undo = c_gen_moves = c_gen_caps = c_gen_quiets = 0;
    c_nn_make = c_nn_undo = c_nn_eval = c_refresh = c_flip = 0;
    c_isattacked = 0;
    c_possig = 0;
    c_rep_scan = c_rep_upscan = c_rep_lookup = 0;
    c_see = c_see_step = 0;
    c_tt_probe = 0;
    c_tt_store = 0;
    c_tt_eval = 0;
#endif
}

/* Conservative allocation shared by the real and virtual clocks. Stop
   after a complete depth at half the normal slice; the hard limit is 1.5x
   that slice. This banks time rather than routinely overspending the average.
   The last move before a refill can use the bank minus output reserve.
   Increment arrives afterwards, so it cannot raise the current bank cap. */
void time_limits_ms(i32 remaining_ms, i16 moves_left, i32 increment_ms,
                    i32 *soft_ms, i32 *hard_ms) {
    i32 available, target;
    if (remaining_ms < 0) remaining_ms = 0;
    if (moves_left < 1) moves_left = 40;
    if (increment_ms < 0) increment_ms = 0;
    available = remaining_ms > TIME_MARGIN_MS ? remaining_ms - TIME_MARGIN_MS : 1;
    target = remaining_ms / moves_left;
    /* Avoid overflowing if a caller supplies a very large increment. */
    if (increment_ms >= available - target) target = available;
    else target += increment_ms;
    if (target < 1) target = 1;
    if (target > available) target = available;
    if (moves_left == 1) {
        *soft_ms = *hard_ms = available;
    } else {
        *soft_ms = target / 2;
        if (*soft_ms < 1) *soft_ms = 1;
        *hard_ms = target > available - target / 2 ? available : target + target / 2;
    }
}

/* Refill only after the requisite moves, regardless of how fast the host
   runs or what time/level/st the GUI sends. Charge actual work afterwards. */
void vclock_limits_ms(i32 *soft_ms, i32 *hard_ms) {
    if (!vperiod_started || vperiod_left <= 0) {
        if (!vperiod_started) {
            vperiod_ms = 7200L * 1000L;
            vperiod_left = 40;
            vperiod_started = 1;
        } else {
            vperiod_ms = 3600L * 1000L;
            vperiod_left = 20;
        }
    }
    time_limits_ms(vperiod_ms, vperiod_left, 0, soft_ms, hard_ms);
}

#ifndef VCLOCK
static i32 vclock_node_limit(i32 ms) {
    i32 cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1], cap;
    if (ms <= 0) return 0;
    cap = (i32)(((u32)ms / 100) * (u32)vcpu_khz / ((u32)cpn / 100));
    /* A tiny or empty bank still has a stop condition. */
    return cap > 0 ? cap : 1;
}
#endif

void vclock_set_limits(i32 soft_ms, i32 hard_ms) {
#ifdef VCLOCK
    vsoft_cyc = (i64)soft_ms * vcpu_khz;
    vbudget_cyc = (i64)hard_ms * vcpu_khz;
#else
    vsoft_nodes = vclock_node_limit(soft_ms);
    vmax_nodes = vclock_node_limit(hard_ms);
#endif
}

/* The soft target never interrupts a partially searched depth. */
i16 vclock_soft_hit(void) {
#ifdef VCLOCK
    return vsoft_cyc > 0 && vclock_cyc() >= vsoft_cyc;
#else
    return vsoft_nodes > 0 && vtotal_nodes >= vsoft_nodes;
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

/* Read the same modeled elapsed time used to charge the bank. Thinking
   output can query this after each depth without changing move bookkeeping. */
i32 vclock_elapsed_ms(void) {
#ifdef VCLOCK
    return (i32)(vclock_cyc() / vcpu_khz);
#else
    i32 cpn = cpn_tab[vcpu_model][nnue_enabled ? 0 : 1];
    if (vtotal_nodes > 0 && cpn >= 100 && vcpu_khz >= 100)
        return (i32)(((u32)vtotal_nodes / 100) * (u32)cpn
                     / ((u32)vcpu_khz / 100));
    return 0;
#endif
}

/* charge the current period the virtual ms the completed move consumed and
   close out its bookkeeping; returns the consumed virtual ms. */
i32 vclock_charge(void) {
    i32 vms = vclock_elapsed_ms();
    vperiod_ms -= vms;
    if (vperiod_ms < 0) vperiod_ms = 0;
    if (vperiod_left > 0) vperiod_left--;
    vtotal_nodes = 0;
    vmax_nodes = 0;
#ifndef VCLOCK
    vsoft_nodes = 0;
#endif
#ifdef VCLOCK
    vbudget_cyc = vsoft_cyc = 0;
#endif
    return vms;
}

#ifdef TIME_TEST
i16 vclock_selftest(void) {
    i16 failures = 0, model, i;
    i32 soft, hard, bank, spent;
    for (model = VCPU_80286; model <= VCPU_80186; ++model) {
        vcpu_model = model;
        vcpu_khz = 25000;
        vclock_reset();
        vclock_set_limits(10000, 30000);
#ifdef VCLOCK
        {
            const VW *w = &vw_tab[model];
            i32 unit = (nnue_enabled ? w->rn : w->rm) + rep_cost[model].fixed;
            i32 at_soft = (i32)((vsoft_cyc + unit - 1) / unit);
            i32 at_hard = (i32)((vbudget_cyc + unit - 1) / unit);
            c_qnodes = at_soft - 1;
            if (vclock_soft_hit() || vclock_budget_hit()) ++failures;
            c_qnodes = at_soft;
            if (!vclock_soft_hit() || vclock_budget_hit()) ++failures;
            c_qnodes = at_hard - 1;
            if (!vclock_soft_hit() || vclock_budget_hit()) ++failures;
            c_qnodes = at_hard;
            if (!vclock_budget_hit()) ++failures;
        }
#else
        vtotal_nodes = vsoft_nodes - 1;
        if (vclock_soft_hit() || vclock_budget_hit()) ++failures;
        vtotal_nodes = vsoft_nodes;
        if (!vclock_soft_hit() || vclock_budget_hit()) ++failures;
        vtotal_nodes = vmax_nodes - 1;
        if (!vclock_soft_hit() || vclock_budget_hit()) ++failures;
        vtotal_nodes = vmax_nodes;
        if (!vclock_budget_hit()) ++failures;
#endif
        vclock_set_limits(0, 0);
        if (vclock_soft_hit() || vclock_budget_hit()) ++failures;
        vclock_reset();
        vclock_set_limits(1, 1);
#ifdef VCLOCK
        c_qnodes = 100;
#else
        vtotal_nodes = 100;
#endif
        if (!vclock_soft_hit() || !vclock_budget_hit()) ++failures;
    }
    vcpu_model = VCPU_80286;
    vcpu_khz = 25000;
    vclock_newgame();
    vclock_limits_ms(&soft, &hard);
    if (soft != 90000L || hard != 270000L || vperiod_left != 40) ++failures;
    bank = vperiod_ms;
    vclock_set_limits(soft, hard);
#ifdef VCLOCK
    c_qnodes = 1000;
#else
    vtotal_nodes = 1000;
#endif
    spent = vclock_charge();
    if (spent <= 0 || vperiod_ms != bank - spent || vperiod_left != 39) ++failures;
    if (vclock_soft_hit() || vclock_budget_hit()) ++failures;
    vclock_limits_ms(&soft, &hard);
    if (soft != (vperiod_ms / 39) / 2 ||
        hard != vperiod_ms / 39 + (vperiod_ms / 39) / 2) ++failures;
    for (i = 1; i < 40; ++i) { vclock_reset(); vclock_charge(); }
    vclock_limits_ms(&soft, &hard);
    if (soft != 90000L || hard != 270000L || vperiod_left != 20) ++failures;
    for (i = 0; i < 20; ++i) { vclock_reset(); vclock_charge(); }
    vclock_limits_ms(&soft, &hard);
    if (soft != 90000L || hard != 270000L || vperiod_left != 20) ++failures;
    vperiod_ms = 1000; vperiod_left = 1;
    vclock_limits_ms(&soft, &hard);
    if (soft != 970 || hard != 970) ++failures;
    vperiod_ms = 0; vperiod_left = 10;
    vclock_limits_ms(&soft, &hard);
    if (soft != 1 || hard != 1 || vperiod_left != 10) ++failures;
    vclock_newgame();
    vclock_limits_ms(&soft, &hard);
    if (soft != 90000L || hard != 270000L || vperiod_left != 40) ++failures;
    printf("virtual selftest: failures=%d\n", failures);
    vclock_newgame();
    return failures;
}
#endif

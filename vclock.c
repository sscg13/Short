/* vclock.c - virtual time for the 40/2h + 20/1h repeating control

   VirtualTime=1 budgets work for CPU_model at CPU_KHz, ignoring GUI time
   commands. Each period replaces the remaining bank: 40 moves in 2 hours,
   then 20 moves in 1 hour, repeating. Soft/hard limits are half/1.5 times
   the average allocation, except that the last move may use the bank.

   There is one model: measured per-operation costs plus a fitted per-node
   residual, with cached TT eval writes and repetition work included. It
   reads the PCOUNT operation counters, so it exists only in VCLOCK builds,
   which in practice means the native build. Virtual time exists to model a
   CPU that the real machine already is, so the 16-bit engine paces itself
   with its real clock instead (timing.c) and the DOS release defines no
   counters: compiling them for a default-off feature measured +2.7% on the
   80186 bench-4 search (artifacts/vclock-fine-only-2026-11-08). The coarse
   cycles-per-node fallback was removed in the same change.

   Recalibrated 2026-11-03 for the build.ps1 -os (favor code size) switch, which
   re-blocks the hot C paths to fall through instead of jumping. Measured
   whole-search cycle ratios (new/old) on the same depth-4 bench tree:
   0.9461 (80286 @6 MHz), 0.9648 (8088 @16 MHz) and 0.9666 (8086 @8 MHz) from
   the per-position search timers, and 0.9570 for the Nimbus 80186 @8 MHz from
   its marker interval minus the same build's `chess 1` interval, which removes
   the 242M-cycle DOS EXE load and leaves the search itself. The hand-written
   NNUE assembly is unaffected by a C compiler flag, so rf (materialized batch)
   and ev (forward pass) keep their measured costs; every other row is C code
   and was scaled by c = (k - s)/(1 - s), where s is that CPU's asm share of
   cycles per node (20.8 / 20.7 / 21.4 / 22.6%). Every rescaled model total
   reproduces its measured k exactly.

   Recalibrated 2026-11-04 for the adopted hand-written is_attacked and gen_caps:
   the vw_tab att/gc rows scale by the measured asm/C ratio and the measured
   whole-search k, and rn/rm are unchanged because the model's own k still
   lands within 1.4% of measured on every CPU where both exist. Dated deltas:
   artifacts/asm-default-2026-11-04/REPORT.md.

   Recalibrated 2026-10-08 after the Watcom attack/SEE, quiet-history, NNUE
   indexing optimizations. Original main search and TT are unchanged.
   Unprofiled -0 -ml -ox measurements:
   86Box interpreter 80286 @6 MHz, 8088 @16 MHz, 8086 @8 MHz; MAME Nimbus
   80186 @8 MHz processor totalcycles. Native PROFILE supplies matching
   counters and all position results with the default v5 net 56329CCE.

   Unchanged board/NNUE/SEE primitives retain verified sbench/nbench costs.
   Whole searches and original public TT paths are remeasured; compiled
   kernel checks guard reuse. Only rn/rm fit whole NNUE/material depths
   2 and 4, with equal relative-error least squares; depth 3 is held out.
   Full setup/output windows and counters remain in the fit. Fixed benchmark
   overhead and varying move-picker work mean one residual does not fit all
   depths exactly.

   Dated inputs, hashes, fit/holdout errors and reproduction:
   artifacts/nonfunctional-main-2026-10-08/REPORT.md and model-audit/proposal.json.
   Emulator calibration does not establish absolute physical-hardware time.
*/

#include "engine.h"

i16 vtime_mode = 0;          /* VirtualTime=1: use the virtual clock */
i32 vcpu_khz = 25000;       /* CPU_KHz: cycles per ms of the modeled CPU */
i16 vcpu_model = VCPU_80286; /* CPU_model index */

i32 vtotal_nodes = 0;       /* nodes searched so far in the current move */

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

#ifdef VCLOCK

/* virtual period state: 40 moves in 2 h, then 20 moves per 1 h, repeating */
static i32 vperiod_ms;      /* virtual ms left in the current period */
static i16 vperiod_left;    /* moves left in the current period */
static i16 vperiod_started; /* first period not yet granted */

static i64 vbudget_cyc, vsoft_cyc; /* weighted hard/soft limits */

typedef struct { i32 att, ps, gc, gq, gm, mk, nm, rf, ev, rn, rm, tp, ts; } VW;
/* Rescaled 2026-11-03 for -os: C-compiled rows x the C-only factor c, while
   the two NNUE-assembly rows rf/ev keep their measured costs (see header).
   rn/rm were refitted 2026-11-07 for frozen quiet-ordering scores (branch
   score-once-quiet): each row is lowered so the model's own depth-4 per-node
   total scales by that CPU's measured whole-search k, with rm moved by the
   same absolute amount as rn because ordering cost is eval-independent. rn
   falls 10531->8016 (80286), 34403->24969 (8088), 28628->20083 (8086) and
   14167->10896 (80186); the other uncharged work (control flow, flip
   recompute, PV/history updates, fit slack) is untouched. See
   artifacts/score-once-quiet-2026-11-07/REPORT.md. */
static const VW vw_tab[4] = {
    /* att ps gc gq gm mk nm rf ev rn rm tp ts: cycles per counted operation */
    {    656,      0,   4964,  14462, 135646,    950,    853,   2922,   5109,   8016,   7550,    652,    652 }, /* 80286 */
    {   1970,      0,  14743,  45138, 465065,   3123,   2813,   9545,  15596,  24969,  19904,   2246,   2389 }, /* 8088 */
    {   1693,      0,  12621,  39086, 413168,   2734,   2419,   8780,  13072,  20083,  16157,   1915,   2011 }, /* 8086 */
    {   1003,    131,   7522,  22434, 215267,   1507,   1382,   5125,   7540,  10896,  10612,   1181,   1228 }, /* 80186 */
};
/* att and gc were refitted 2026-11-04 when the hand-written is_attacked and
   gen_caps became the default. Both rows are scaled by the measured asm/C ratio
   from the Nimbus sbench markers: att 1,081 / 2,274 = 0.4754, gc 7,188 / 16,531
   = 0.4348. Scaling rather than assigning the absolute per-call cost keeps the
   whole-search level this table was fitted at (that level is what rn/rm absorb,
   and gc's row has always sat above the isolated sbench figure). At refit time
   the per-operation costs were only measurable on the 80186 - 86Box's guest
   clock() resolved at 55 ms and could not time sub-second sbench loops (it
   reported a negative att) - so the same ratio is applied to every CPU. The
   DOS build now reads the PIT counter directly (timing.c), so those loops are
   separately measurable on 86Box too. Check: the model's own k then lands
   within 0.6% of the measured whole-search k on both CPUs where both exist
   (80186 0.8875 vs 0.8919; 8088 0.8967 vs 0.8913), so rn/rm stay as fitted.
   att was refitted again 2026-11-04 for the probe reorder (pawn probes folded
   into the two diagonal rays, which now run first): 1,081 -> 1,041 on the
   Nimbus markers, ratio 0.96300, applied to every CPU's row by the same rule.
   No new reordering is expected to be worth a third pass; that report also
   records the measured per-CPU whole-search k for the rescale above.
   gc was refitted again 2026-11-04 for the sweep/dispatch rewrite (unrolled
   rank sweep with resume table, dispatch table, STOSW emits): 7,189 -> 6,167
   on the Nimbus markers, ratio 0.85784, applied to every CPU's row by the same
   rule. See artifacts/gencaps-sweep-2026-11-04/REPORT.md. */
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
   from their earlier measured/scaled calibration because that code is unchanged.

   rf was shifted 2026-10-31 by the measured lodsb/stosw per-batch change
   (same 1.343 batches/node conversion): -803 (8088), -755 (8086),
   -225 (80286) and +66 (80186, capture bodies lose to a disp8 load while the
   quiet body's stosw still wins). nm and ev are untouched because planning
   and the forward pass did not change, and rn/rm stay as fitted: they are the
   per-node residual, so moving rf by the measured per-batch change moves the
   whole-search prediction by the same amount as the real machine. */

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
    {  612,  116,  100,   932 }, /* 286 */
    { 3931,  748,  640,  5984 }, /* 8088 */
    { 3144,  598,  512,  4785 }, /* 8086 */
    { 1066,  203,  174,  1623 }, /* 80186 */
};

/* SEE: verified unchanged uninstrumented 4000-call fixtures on all CPUs, with
   native PROFILE call/scan/attack counts. Entry rounds up to 50 cycles.
   Remove entry and measured attack costs from each fixture; round the slowest
   scan residual up to 1000 cycles with at least 20% headroom. The full rate
   is charged even when a pawn/knight scan exits early. Reproduce with
   -DSEE_TEST and `chess seebench`; rn/rm also undergo the whole-search fit. */
typedef struct { i32 entry, step; } SeeCost;
static const SeeCost see_cost[4] = {
    {  419,  2796 }, /* 80286: measured entry / conservative scan */
    { 1290,  8601 }, /* 8088: measured entry / conservative scan */
    { 1053,  7660 }, /* 8086: measured entry / conservative scan */
    {  567,  4722 }, /* 80186: measured entry / conservative scan */
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

void vclock_set_model(const char *name) {
    if (!name) return;
    if (strcmp(name, "8088") == 0) vcpu_model = VCPU_8088;
    else if (strcmp(name, "8086") == 0) vcpu_model = VCPU_8086;
    else if (strcmp(name, "80186") == 0) vcpu_model = VCPU_80186;
    else vcpu_model = VCPU_80286;   /* "80286" or anything else */
}

void vclock_set_khz(i32 khz) {
    if (khz < 1000) khz = 1000;       /* keep the /100 scalings in range */
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

/* zero the per-move state and the weighted counters */
void vclock_reset(void) {
    vtotal_nodes = 0;
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

void vclock_set_limits(i32 soft_ms, i32 hard_ms) {
    vsoft_cyc = (i64)soft_ms * vcpu_khz;
    vbudget_cyc = (i64)hard_ms * vcpu_khz;
}

/* The soft target never interrupts a partially searched depth. */
i16 vclock_soft_hit(void) {
    return vsoft_cyc > 0 && vclock_cyc() >= vsoft_cyc;
}

/* 1 when the current move has consumed its virtual budget */
i16 vclock_budget_hit(void) {
    return vbudget_cyc > 0 && vclock_cyc() >= vbudget_cyc;
}

/* Read the same modeled elapsed time used to charge the bank. Thinking
   output can query this after each depth without changing move bookkeeping. */
i32 vclock_elapsed_ms(void) {
    return (i32)(vclock_cyc() / vcpu_khz);
}

/* charge the current period the virtual ms the completed move consumed and
   close out its bookkeeping; returns the consumed virtual ms. */
i32 vclock_charge(void) {
    i32 vms = vclock_elapsed_ms();
    vperiod_ms -= vms;
    if (vperiod_ms < 0) vperiod_ms = 0;
    if (vperiod_left > 0) vperiod_left--;
    vtotal_nodes = 0;
    vbudget_cyc = vsoft_cyc = 0;
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
        vclock_set_limits(0, 0);
        if (vclock_soft_hit() || vclock_budget_hit()) ++failures;
        vclock_reset();
        vclock_set_limits(1, 1);
        c_qnodes = 100;
        if (!vclock_soft_hit() || !vclock_budget_hit()) ++failures;
    }
    vcpu_model = VCPU_80286;
    vcpu_khz = 25000;
    vclock_newgame();
    vclock_limits_ms(&soft, &hard);
    if (soft != 90000L || hard != 270000L || vperiod_left != 40) ++failures;
    bank = vperiod_ms;
    vclock_set_limits(soft, hard);
    c_qnodes = 1000;
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
#endif /* TIME_TEST */

#else /* !VCLOCK */

/* No counters, so no model: VirtualTime is a native-build feature and the
   16-bit engine uses its real clock. Keep the interface so shared call sites
   compile, and leave vtime_mode off even if a GUI sends the option. */

void vclock_set_model(const char *name) { (void)name; }
void vclock_set_khz(i32 khz) { (void)khz; }
void vclock_set_enabled(const char *val) { (void)val; }
void vclock_newgame(void) { }
void vclock_reset(void) { vtotal_nodes = 0; }
void vclock_limits_ms(i32 *soft_ms, i32 *hard_ms) { *soft_ms = *hard_ms = 0; }
void vclock_set_limits(i32 soft_ms, i32 hard_ms) { (void)soft_ms; (void)hard_ms; }
i16 vclock_soft_hit(void) { return 0; }
i16 vclock_budget_hit(void) { return 0; }
i32 vclock_elapsed_ms(void) { return 0; }
i32 vclock_charge(void) { return 0; }

#ifdef TIME_TEST
i16 vclock_selftest(void) {
    printf("virtual selftest: skipped (VCLOCK off)\n");
    return 0;
}
#endif

#endif /* VCLOCK */

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

   RE-BASED 2026-11-10 against absolute emulator totals (not ratios). Measuring
   depth-4 search-only cycles on all four CPUs showed the model 21-26% high,
   with the charged rows within 6% of measured: the excess was the fitted rn
   plus conservative SEE/rf/gc/gq rows. Those rows were re-measured in situ
   (see the note above vw_tab) and rn/rm refitted, so the model now reproduces
   the measured depth-4 cycles instead of only build-to-build ratios.
   See artifacts/vclock-fit-2026-11-09/REPORT.md.

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
   artifacts/score-once-quiet-2026-11-07/REPORT.md.

   rn/rm were lowered again 2026-11-09 by the ordering row added below
   (ord_cost), which now charges the measured ordering steps explicitly
   instead of leaving them inside the residual: rn 8016->6192 (80286),
   24969->18971 (8088), 20083->14831 (8086), 10896->8001 (80186); rm by the
   same absolute amount. The depth-4 model total is unchanged; other depths
   move by <0.5%. See artifacts/vclock-ordering-2026-11-09/REPORT.md.

   TT call-site logic (probe hit handling, cutoff dispatch, store gate) was
   measured 2026-11-09 with `chess ttbench` on the 80186 markers: ~6 cyc/node
   on the 80286, 0.02% of the model, because the probe/store bodies are
   already charged by tp/ts and the model overcharges probe misses and
   early-return stores by more than that. It deliberately stays inside rn;
   no tt_cost row. See artifacts/tt-callsite-2026-11-09/REPORT.md.

   RE-BASED 2026-11-10. The model read 21-26% above the measured search on all
   four CPUs at depth 4 (80286 26,125 vs 21,257; 8088 84,317 vs 67,787; 8086
   71,687 vs 57,945; 80186 40,099 vs 33,157), while the charged rows on their
   own were within 6% of measured on every CPU. The excess was therefore the
   fitted rn plus three rows that had drifted into pessimism. Everything below
   was re-measured, not rescaled by a whole-search ratio:
     - SEE: every see_ge call bracketed on the Nimbus (markers 0x60/0x61),
       4,207 cycles/call against 5,299 charged; step 4,722 -> 3,633 (80186),
       other CPUs keep their step ratio.
     - rf: every materialized batch bracketed (markers 0x62/0x63), 4,669
       cycles/batch against 5,125; 63,405 batches per `bench 4`, matching
       c_refresh.
     - gc/gq: sbench 6,170 / 18,639 on the 80186 and 4,614 / 12,366 on the
       80286, against 7,522 / 22,434 and 4,964 / 14,462.
   rn/rm were then refitted to the measured whole-search totals at depth 4:
   rn 6,192 -> 2,293 (80286), 18,971 -> 5,948 (8088), 14,831 -> 4,214 (8086),
   8,001 -> 2,931 (80186); rm by the same ratio, so the rn/rm relationship the
   earlier fits established is preserved. The residual is now 7-11% of each
   search (uncharged control flow) instead of 21-26%, and the model total
   reproduces the measured depth-4 cycles on all four CPUs to the rounding of
   this table. See artifacts/vclock-fit-2026-11-09/REPORT.md.
   Any edit to these tables or to see_cost/rep_cost/ord_cost must be followed by
   `chess calcheck` (part of `make test`), which compares this model against the
   dated references below; TESTING.md section 7 has the re-measurement recipe.

   RE-MEASURED 2026-11-10 after the NNUE flip rebuild moved into asm
   (`nn_row_add_`, one weight row into an accumulator): the same depth-4 search
   totals were taken again on the new binary and rn/rm were refit to them, so
   the model tracks the faster engine rather than the old one:
     80286 21,257 -> 20,649 (86Box atami, -2.9%)
     8088  67,787 -> 65,668 (86Box xt,    -3.1%)
     8086  57,945 -> 56,134 (derived from the 8088 by the same ratio)
     80186 33,157 -> 32,682 (Nimbus markers, -1.4%)
   rn absorbed the saving on every CPU (80286 2,293 -> 1,685, 8088 5,948 ->
   3,829, 8086 4,214 -> 2,403, 80186 2,931 -> 2,456). rm is unchanged: the
   mirror-flip rebuild only exists with NNUE on. The 80186 delta is smaller
   than the other CPUs' because the flip body is memory-bound there; measured
   in situ it fell 107,179 -> 62,312 cycles per rebuild (-42%).

   The same day the whole rebuild (bias copy + 0x88 walk) moved into that asm
   routine too (`nn_compute_persp_slot_`), so the totals moved again and rn was
   refit a second time:
     80286 20,649 -> 20,516 (86Box atami, -0.6%)
     8088  65,668 -> 65,229 (86Box xt,    -0.7%)
     8086  56,134 -> 55,758 (derived)
     80186 32,682 -> 32,492 (Nimbus markers, -0.6%)
   rn 1,685 -> 1,552 (80286), 3,829 -> 3,390 (8088), 2,403 -> 2,027 (8086),
   2,456 -> 2,266 (80186). A flip rebuild is now 52,882 cycles (from 107,179)
   and the walk, bias copy and per-row call overhead that used to cost 14k of
   that are gone.

   RE-MEASURED 2026-11-10 again for the make path's feature-row planning
   (nnue_make's plan recording moved into hand-written asm, `nnue_plan.asm`):
     80286 20,516 -> 19,659 (86Box atami, -4.2% guest time)
     8088  65,229 -> 62,876 (86Box xt,    -3.6%)
     8086  55,758 -> 53,747 (derived from the 8088 by the same ratio)
     80186 32,492 -> 31,271 (Nimbus markers, -3.8%)
   This saving lands on `nm`, which is exactly the row that prices per-make
   planning (nm is charged per nn_make + nn_undo call, 121,816 times per
   bench 4): nm 853 -> 520 (80286), 2,813 -> 1,897 (8088), 2,419 -> 1,636
   (8086), 1,382 -> 907 (80186), i.e. the measured total change divided by the
   call count. rn/rm are untouched: they are the per-node residual, and the
   part of the plan cost they carry is unchanged in kind. */
static const VW vw_tab[4] = {
    /* att ps gc gq gm mk nm rf ev rn rm tp ts: cycles per counted operation */
    {    656,      0,   4614,  12366, 135646,    950,    520,   2662,   4504,   2258,   2837,    652,    652 }, /* 80286 */
    {   1970,      0,  12093,  37502, 465065,   3123,   1897,   8696,  15476,   5800,   6805,   2246,   2389 }, /* 8088 */
    {   1693,      0,  10353,  32474, 413168,   2734,   1636,   7999,  12971,   3948,   5076,   1915,   2011 }, /* 8086 */
    {   1003,    131,   6170,  18639, 215267,   1507,    907,   4669,   6999,   3301,   3874,   1181,   1228 }, /* 80186 */
};
/* 2026-11-12: counter-move ordering (artifacts/counter-move-2026-11-12/).
   The quiet scorer gained one compare per candidate and mgen_init one indexed
   word load per node, which is a per-node cost that no priced operation
   covers, so it lands in the residual: rn/rm both +74 (80286), +263 (8088),
   +227 (8086), +117 (80186) - the measured minus modelled gap on the new
   tree, applied to both rows because they multiply the same node total
   (rn for NNUE nodes, rm otherwise) and the work is eval-independent. The
   references were re-measured on the 38,362-node tree first, so this is a
   level refit, not a rescale: emulated +0.35% (80286), +0.41% (8088),
   +0.35% (80186). */
/* 2026-11-12: PV nodes now take late-move reductions one ply softer than cut
   nodes, with the root exempt (artifacts/pv-lmr-root-2026-11-11/REPORT.md).
   That is a per-node mix change, not a change to any priced operation, so only
   the residual moved: `rn` was refitted to the re-measured depth-4 totals and
   `rm` scaled by the same ratio, rn 1,552 -> 1,582 (80286), 3,390 -> 3,514
   (8088), 2,027 -> 2,133 (8086), 2,266 -> 2,316 (80186). The pre-refit model
   was only 0.15% below the new references on every CPU, because the tree lost
   19% of its nodes but kept the same cost mix. */
/* 2026-11-11: the forward pass's 255 clamp became branchless (gen_nnue_batch.py
   `_fwd_body`), so `ev` was re-measured and dropped by 605 / 120 / 101 / 541
   cycles per eval on 80286 / 8088 / 8086 / 80186 (`chess fbench`: markers on the
   Nimbus, guest-ms on 86Box; the 8086 scales its 8088 share). The depth-4
   references in vclock_cal_cyc are that build's bench-4 totals, so the model
   still reproduces the measured level (calcheck +0%). No other row moved: the
   change is entirely inside nn_fwd_eval. */
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
   rule. See artifacts/gencaps-sweep-2026-11-04/REPORT.md.
   That deliberate "keep the whole-search level" offset was removed on
   2026-11-10: gc/gq now carry the measured per-call cost and rn carries only
   the uncharged control flow (see the re-base note above). */
/* nm: average quiet/capture plan pair minus its matched board pair, /2.
   rf: mean quiet/capture (materialized pair - matched plan pair), /2 perspectives.
   Each pair materializes two perspective batches; undo does not materialize.
   Since 2026-11-10 rf is the measured in-situ average batch (every quiet,
   capture and castle apply call bracketed on the Nimbus: 4,669 cycles per
   batch in a depth-4 `bench 4`), so the quiet/capture split no longer affects
   it. Quiet/capture and mirror-flip differences remain approximations in rn/rm.
   tp rounds the slowest measured probe up to 50 cycles. ts similarly covers
   the maximum original public result/eval path, including reused eval and
   mates. Result-store samples include the public key-fold overhead.
   86Box ps remains zero and absorbed in rn/rm: these searches have no pos_sig
   calls, and isolated loops include overhead. Nimbus uses its exact marker
   interval. Repetition terms below are retained
   from their earlier measured/scaled calibration because that code is unchanged.

   rf was shifted 2026-10-31 by the measured lodsb/stosw per-batch change
   (same 1.343 batches/node conversion): -803 (8088), -755 (8086),
   -225 (80286) and +66 (80186, capture bodies lose to a disp8 reg while the
   quiet body's stosw still wins). nm and ev are untouched because planning
   and the forward pass did not change, and rn/rm stay as fitted: they are the
   per-node residual, so moving rf by the measured per-batch change moves the
   whole-search prediction by the same amount as the real machine.

   2026-11-13, late move pruning (artifacts/lmp-2-2026-11-13/REPORT.md). The
   depth-4 tree shrinks 38,352 -> 29,296 nodes (-23.6%) but each node gets more
   expensive, because a pruned candidate pays a make, a legality test, the
   check test and an undo before it is skipped: measured cycles per node rise
   +18.5% (80286 19,742 -> 23,394; 8088 64,289 -> 76,206; 80186 31,639 ->
   37,342; 8086 derived 65,166), while the whole depth-4 search still gets
   ~9.5% faster (atami 126.20 s -> 114.23 s). The make/undo, NNUE-plan and
   is_attacked work is already charged by mk/nm/att and scaled with the new
   counters by itself - the model with the old rows came out only 2% low on
   all four CPUs, uniformly per node, which is the unpriced gate compares
   (~4-6 per move at shallow nodes). That gap is the residual, so rn and rm
   both take the measured per-node delta: +602 (80286), +2,023 (8088),
   +1,588 (8086), +868 (80186). No other row moved. */

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

/* SEE: entry plus a per-scan step. Until 2026-11-10 the step was the slowest
   fixture scan plus 20% headroom, charged even when a pawn/knight scan exits
   early; measured in situ that came out 26% high. The step is now the in-situ
   average: every see_ge call in a Nimbus `bench 4` was bracketed with port-E9
   markers (0x60/0x61), giving 4,207 cycles/call at 1.00 scans/call, so the
   80186 step is (4207 - 567) / 1.002 = 3,633 and the other CPUs keep their old
   step ratio (x0.7694). Entry stays at the measured prologue. The 80186 is
   measured; the others cannot run the markers, so they scale by ratio, the
   same rule the att/gc rows use. See artifacts/vclock-fit-2026-11-09. */
typedef struct { i32 entry, step; } SeeCost;
static const SeeCost see_cost[4] = {
    {  419,  2151 }, /* 80286: measured entry / scan scaled from 80186 */
    { 1290,  6617 }, /* 8088: measured entry / scan scaled from 80186 */
    { 1053,  5893 }, /* 8086: measured entry / scan scaled from 80186 */
    {  567,  3633 }, /* 80186: measured entry / measured in-situ scan */
};

/* Move ordering, split out of the rn/rm residual on 2026-11-09. Steps are the
   measured 80186 port-E9 marker prices (orderbench-2026-11-07, after the
   mvv_lva inline, and score-once-quiet-2026-11-07 for the frozen quiet stage):
   capture-stage comparison 434, quiet score computation 197, quiet sort
   comparison 151, quiet return 129, killer validation 963, and the
   next_move call/prologue estimate 470 (which also covers the two TT-stage
   returns). Only the 80186 can be priced per step, so the other CPUs scale by
   their board make/undo row over the 80186 one (950/1507, 3123/1507,
   2734/1507) - the same measured-ratio rule the att/gc refits use. rn/rm are
   lowered by each CPU's depth-4 ordering charge, so the model total is
   unchanged at the fit point; depths 2-8 drift by under 0.6% and depth 1 (all
   root and qsearch, no quiet stage) undercharges by 3.4% because ordering
   operations per node vary with the tree shape.
   See artifacts/vclock-ordering-2026-11-09/REPORT.md. */
typedef struct { i32 cap, qsort, qret, qscore, killer, call; } OrdCost;
static const OrdCost ord_cost[4] = {
    {  274,    95,   81,    124,    607,  296 }, /* 80286 */
    {  899,   313,  267,    408,   1996,  974 }, /* 8088 */
    {  787,   274,  234,    357,   1747,  853 }, /* 8086 */
    {  434,   151,  129,    197,    963,  470 }, /* 80186 */
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
    {
        const OrdCost *o = &ord_cost[vcpu_model];
        r += (i64)o->cap    * c_nm_capscan;
        r += (i64)o->qsort  * c_nm_qsort;
        r += (i64)o->qret   * c_nm_qret;
        r += (i64)o->qscore * c_nm_qscan;
        r += (i64)o->killer * c_nm_killer;
        r += (i64)o->call   * c_nextmove;
    }
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

/* ABSOLUTE-LEVEL GATE (`chess calcheck`, run by `make test`).
   The model used to be re-fitted only to the measured *ratio* k between two
   builds, which preserves whatever absolute level the table happens to have.
   On 2026-11-10 that had drifted to +21-26% while every ratio check passed.
   These are dated, search-only depth-4 totals per modeled CPU -- every node in
   the `bench 4` tree, i.e. sum over d = 1..4 of all 12 positions:

     build.ps1 [-MameMarkers] release, 86Box / MAME Nimbus, see
     artifacts/vclock-fit-2026-11-09/REPORT.md, re-measured 2026-11-10 after the
     NNUE flip-rebuild asm (artifacts/nnue-rowadd-2026-11-10/REPORT.md) and again
     after the asm make-path planning (artifacts/nnue-plan-2026-11-10/REPORT.md),
     then 2026-11-12 on the PV-LMR tree, 38,348 nodes
     (artifacts/pv-lmr-root-2026-11-11/REPORT.md), and again 2026-11-12 on the
     counter-move tree, 38,362 nodes
     (artifacts/counter-move-2026-11-12/REPORT.md), and again 2026-11-12 on the
     follow-up tree, 38,352 nodes
     (artifacts/follow-up-2026-11-12/REPORT.md):
       80286 @6 MHz  19,742 cyc/node   8088 @16 MHz  64,289
       8086  @8 MHz  54,971 (derived)  80186 @8 MHz  31,639

   The counter-move tree's totals are ~0.4% above the PV-LMR ones on every CPU:
   the quiet scorer gained one compare per candidate and mgen_init one indexed
   word load per node, which is what the residual below absorbs. The follow-up
   table sits beside that compare and costs nothing measurable: the new totals
   are within 0.08% of the counter-move ones, so the residual is unchanged.

   If the tree changes (bench node total differs) the reference no longer
   applies: re-measure it, do not "fix" the model to match a stale tree. */
#define VCLOCK_CAL_NODES   23942
#define VCLOCK_CAL_TOL_PCT 3
static const i32 vclock_cal_cyc[4] = {
    22489,  /* 80286 */
    73016,  /* 8088 */
    62434,  /* 8086 (derived: 8088 x 54,971/64,289) */
    35756,  /* 80186 */
};
static const char *const vclock_cpu_name[4] = {
    "80286", "8088", "8086", "80186"
};

/* Compare the current counters against the dated references (1 = failure).
   Must be called with the depth-4 bench counters still accumulated. */
i16 vclock_calcheck(i32 nodes) {
    i16 m, fails = 0;
    if (!nnue_enabled) {
        printf("calcheck: no NNUE net loaded; the references are NNUE-search totals\n");
        return 1;
    }
    if (nodes != VCLOCK_CAL_NODES) {
        printf("calcheck: SKIP - bench tree is %ld nodes, reference is %d; "
               "the model level is NOT verified. Re-measure the emulator "
               "reference and update vclock_cal_cyc + VCLOCK_CAL_NODES "
               "(TESTING.md section 7)\n", (long)nodes, VCLOCK_CAL_NODES);
        return 0;
    }
    for (m = VCPU_80286; m <= VCPU_80186; ++m) {
        i16 saved = vcpu_model;
        i32 got, err;
        vcpu_model = m;
        got = (i32)(vclock_cyc() / nodes);
        vcpu_model = saved;
        err = (i32)(((i64)got - vclock_cal_cyc[m]) * 100 / vclock_cal_cyc[m]);
        printf("calcheck %-5s model=%6ld measured=%6ld err=%+3ld%%%s\n",
               vclock_cpu_name[m], (long)got, (long)vclock_cal_cyc[m], (long)err,
               (err > VCLOCK_CAL_TOL_PCT || err < -VCLOCK_CAL_TOL_PCT) ? "  FAIL" : "");
        if (err > VCLOCK_CAL_TOL_PCT || err < -VCLOCK_CAL_TOL_PCT) ++fails;
    }
    if (fails)
        printf("calcheck: %d CPU(s) outside +/-%d%%; re-measure and re-fit the "
               "model rows, do not rescale rn alone\n",
               (int)fails, VCLOCK_CAL_TOL_PCT);
    return fails;
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
    c_tt_hit = 0;
    c_tt_cut = 0;
    c_nm_tt = c_nm_capscan = c_nm_qscan = c_nm_qret = c_nm_killer = c_nm_qsort = 0;
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

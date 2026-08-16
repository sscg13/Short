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
   8086 est = 0.75x of the 8088 delta. Bench 4 = 653,046 (was 880,565).

    RFP (2026-08, branch `rfp`): reverse futility pruning in alphabeta
    (depth 1..7, non-PV, not in check: return eval when eval - 100*depth >=
    beta) cuts bench 4 653,046 -> 382,982 but leaves bench 1 UNCHANGED at 13,458
    (RFP needs depth >= 1, so a depth-1 search never fires it). No re-fit needed:
    the weighted model auto-tracks the added alphabeta-node evals (they route
    through nnue_eval -> c_nn_eval -> `ev`) and the node-count drop (c_anodes ->
    `rn`); the scalar cpn_tab is still calibrated on the unchanged bench-1. The
    one deliberate estimate: for deeper searches the material build's added
    alphabeta evals are uncharged (folded into the fitted base R, as always).

    RE-MEASURE re-fit (2026-08, branch `nmp`): fresh sbench/nbench + bench 1 +
    bench 2 on the emulators (8088 vm\xt @16 MHz, 80286 vm\atami @6 MHz) with the
    NMP build. The search weights (att/gc/gq/mk/ps/tp/ts) re-verified within 2-5%.
    The NNUE weights were ~1.5-1.8x STALE (the forward and the delta path had
    drifted from the old nbench numbers): fresh eval 35,360 / 10,296 cyc, make+undo
    pair 43,408 / 13,674 (vs 19,328 / 6,588 and the old charge ~31,900 / ~10,700).
    With the old weights the model UNDER-predicted the measured bench-2 total by
    ~7%; the fresh weights agree within ~1.6% (bench 1: 1.976e9 c88 / 0.641e9 c286
    @13,458 nodes; bench 2: 2.227e9 c286 @46,032 nodes). nm+rf set from the nbench
    pair minus the sbench board pair - only their SUM is measured (c_nn_make ~=
    c_refresh ~= 2x makes, so the nm/rf split does not move the total); ev from
    nbench eval. gm set to the fresh full-drain cost (charged only at the 8 root
    calls - negligible). rn re-fit so bench-1 reproduces the fresh totals: rn 2,516
    (286) / 5,855 (8088). The scalar cpn_tab NNUE row re-derived from the same
    totals (was ~12-20% low). The material row (rm, cpn material) is UNCHANGED -
    not re-measured (NNUE is the default build; -DNO_NNUE is a dev-only flag).
    Mirror flips (c_flip -> nn_compute_persp) are still UNCHARGED, as before
    (rare: 157/depth-1, ~19K/depth-5; the ~1-2% cost is absorbed in rn).

     PLY-INDEXED ACCUMULATOR note (2026-08): the copy-make snapshots were replaced
     by writing the child accumulator into nn_acc[ply+1] (undo = nn_ply--, no
     memcpy), which cut the 8088 NNUE make+undo pair ~5% (43,408 -> 41,216 c88;
     the 286 is ~flat). The nm/rf weights below were fit to the OLD pair, so they
     now over-charge the 8088 delta path by ~5% (~1.5% of the total - absorbed in
     rn at bench-1, but re-fit nm/rf (and rn) if depth extrapolation matters.

     TT CUTOFFS note (2026-08, branch `tt-cutoff`, main a212243): TT probes now
     also return EXACT/bound cutoffs at non-PV (zero-window) nodes when the
     stored depth >= the node depth (PV nodes keep move-ordering only). Bench 1
     is UNCHANGED at 13,458 (no transpositions at depth 1); bench 4/5 drop
     382,982 -> 356,316 and 1,688,724 -> 1,549,445. No re-fit: the model tracks
     the probe (tp) and store (ts) calls and the node-count drops; the cutoffs
     add no new primitive.

     NMP retry note (2026-08, branch `nmp-again`): null-move pruning in alphabeta
     between RFP and the move loop. NMP_DEPTH 2 (active at depth >= 2, so bench 4
     exercises it - the first attempt's depth-4 gate left bench 4 unchanged),
     NMP_RED 2 + depth/6, non-PV, not in check, side to move holds non-pawn
     material, eval >= beta + 60 (the slack keeps the shallow qsearch probes from
     the quiet-defense blunder that regressed bench pos 5 in the first attempt's
     qsearch probe). Depth 4+ probes are real searches (nd >= 1); depth 2-3 probe
     by qsearch (nd <= 0). Bench 1 unchanged at 13,458 (NMP needs depth >= 2);
     bench 4 = 367,867 (+3.2% - shallow probes cost a little), bench 5 =
     1,347,275 (-13.1% vs the 1,549,445 TT-cutoff baseline). Scores match the
     reference on 7/8 bench positions (pos 4 only: 152 -> 270). No re-fit: the
     probes are ordinary alphabeta/qsearch calls (rn/qn) and the shared RFP/NMP
     eval routes through nnue_eval -> c_nn_eval -> `ev`.

     LMR note (2026-08, branch `lmr`): late move reductions in the alphabeta
     move loop. The reduction comes from a static precomputed table lmr_tab
     (R = int(0.75 + ln(d)*ln(m)/2), 2 KB const in DGROUP) - a compile-time
     constant, so gcc and the 16-bit build agree bit-for-bit with no runtime
     log(). Gates: node depth >= 3, legal-move # >= 4, non-PV (zero window),
     not in check, quiet only (mfl == 0 && cap empty), reduced depth clamped to
     >= 1 (a qsearch probe on a quiet move only sees captures - the NMP lesson).
     Bench 1 UNCHANGED at 13,458 (depth 1 never fires LMR); bench 4/5 drop
     367,867 -> 358,845 and 1,347,275 -> 1,022,907 (the ~24% bench-5 drop is the
     point). No re-fit: LMR only reuses existing primitives (rn/qn via ordinary
     alphabeta calls, table reads are free) and the node-count drop is tracked
     by c_anodes -> rn; bench-1 (the scalar cpn calibration) is untouched.

     ReLU^2 eval note (2026-08): the v2 net blob (version 2) adds a second
     activation family: w1 x256, act = clamp(acc,0,255), term = (act^2*w2)>>9
     (pre-shifted in the forward table to fit i16; NNUE_ACT2_SHIFT 9 keeps w2
     at x64 and the final scale at 1.0 = 256 cp - the total is log2(64*256^2/
     256) = 14 bits). The v2 16-bit forward is the generated nn_fwd_eval2_
     (same cost class as v1's nn_fwd_eval_ - actually slightly CHEAPER: no
     shift-only +/-128 sat path, every term is one table load), so the ev/qn
     weights need NO re-fit. The scalar oracle and asm are bit-identical
     (bench-verified on the v2 net: bench 5 = 637,928 vs 1,022,907 for v1 -
     the tree differs because the eval differs, not the timing model). */


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
    { 47600L,  23802L },   /* VCPU_80286  (RE-MEASURE re-fit: bench-1 0.641e9/13458) */
    { 146800L, 76064L },   /* VCPU_8088   (RE-MEASURE re-fit: bench-1 1.976e9/13458) */
    { 114300L, 51198L },   /* VCPU_8086 (estimate, 0.779x of the 8088 row) */
};
#endif

#ifdef VCLOCK
static i64 vbudget_cyc;   /* weighted cycle budget for the current move */

typedef struct { i32 att, ps, gc, gq, gm, mk, nm, rf, ev, rn, rm, tp, ts; } VW;
static const VW vw_tab[3] = {
    /*   att    ps     gc     gq      gm     mk    nm   rf    ev     rn      rm      tp    ts */
    {  2292,   102, 16608, 18942, 169038, 1944, 1400, 3493, 10296,   2516,   8723,   612,  510 }, /* 80286 */
    {  6576,   816, 46128, 54560, 608192, 6176, 4500, 11028, 35360,   5855,  31934,  2032, 1744 }, /* 8088 */
    {  4932,   612, 34596, 40920, 456144, 4632, 3375,  8271, 26520,   4391,  23951,  1524, 1308 }, /* 8086 est */
};
/* RE-MEASURE re-fit (2026-08, branch nmp): all weights are FRESH emulator
   measurements with the NMP build. Search weights from sbench (averaged over the
   8 bench positions, 8088 @16 / 286 @6 MHz): att/gc/gq/mk/ps/tp/ts re-verified
   within 2-5% of the previous table. NNUE weights from nbench: ev = forward pass;
   nm+rf = (make+undo pair minus the sbench board pair)/2 - only the SUM is
   measured, the split (nm:rf ~= 1:2.4) is carried over from the old table and
   does not move the total since c_nn_make ~= c_refresh ~= 2x makes. gm = the full
   staged-drain cost (charged only at the 8 root gen_moves calls). rn re-fit so
   bench-1 reproduces the fresh totals (1.976e9 c88 / 0.641e9 c286 @ 13,458 nodes);
   rm (material base) UNCHANGED - not re-measured. 8086 = 0.75x of the 8088 row. */

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

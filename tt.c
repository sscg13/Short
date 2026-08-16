/* tt.c - transposition table (Zobrist-keyed position cache)

   One 64 KB far table on the 16-bit target (4096 x 16-byte entries, a single
   far segment in the large model), a plain static array on the gcc build.

   A TT is a cache, not a content-addressed table: the index is a fold of the
   position signature's words, and a slot is used iff its stored key equals the
   probed position's key. Single tier by design - a near/L1 tier would only
   shorten the probe for positions the far table already answers, and near data
   (DGROUP, ~19 KB free) is budgeted for strength, not cache tiers (see
   MEMORY.md / OPTIMIZATION.md).

   Entry (16 bytes on both builds):
     key(8) | move(2) | score(2) | eval(2, reserved) | info(1) | pad(1)
   info = depth (6 bits, 0..63) | flag (2 bits: EXACT/LOWER/UPPER).

   The stored key is the full incremental position signature (Pos.sig), which
   includes side/castle/ep, so a key match means the same position - the stored
   move is therefore pseudo-legal without a separate validation pass.

   Replacement keeps the deeper entry. The table is cleared per bench position
   and per game (xboard new/setboard), so no age bits are needed.

   Determinism: the probe/store logic is plain C, identical on gcc and the
   16-bit target, so node counts stay byte-identical across builds. The TT
   shrinks the tree, so the bench node counts and the vclock model must be
   re-verified (see vclock.c calibration notes). */

#include "engine.h"

#define TT_ENTRIES   4096          /* 65536 / 16 */
#define TT_MASK      (TT_ENTRIES - 1)

typedef struct {
    Sig key;               /* full position signature */
    u16 move;              /* best move (engine encoding) */
    Score score;           /* score with bound; mate values node-relative */
    Score eval;            /* static eval; reserved (0) for now */
    u8 info;               /* depth (0..63) | flag << 6 */
    u8 pad;
} TTEnt;

#if defined(__WATCOMC__) && !defined(__386__)
static TTEnt _far tt_table[TT_ENTRIES];   /* one 64 KB far segment */
#else
static TTEnt tt_table[TT_ENTRIES];
#endif

#if defined(PROFILE) || defined(VCLOCK)
i32 c_tt_probe = 0;                 /* tt_probe entries */
i32 c_tt_store = 0;                 /* tt_store entries */
#endif

#define TT_DEPTH(i)  ((i16)((i) & 0x3F))
#define TT_FLAG(i)   ((i16)((i) >> 6))
#define TT_INFO(d,f) ((u8)(((d) & 0x3F) | ((f) << 6)))

/* index: fold the four 16-bit words of the key (no 64-bit shifts or divides
   on the 16-bit target) and mask to the power-of-two size. The words are
   16-bit on both builds so an 8-byte copy fills them exactly. */
static i16 tt_index(Sig key) {
    u16 w[4];
    memcpy(w, &key, 8);
    return (i16)((w[0] ^ w[1] ^ w[2] ^ w[3]) & TT_MASK);
}

/* Mate-score conversion between the search's ROOT-relative encoding and the
   entry's NODE-relative encoding. The search scores a position by the ply of
   the MATED LEAF: a node at ply P with mate distance d stores/returns
   +-(MATE - (P + d)) (the sign says who mates; P+d is the leaf's ply), and the
   root prefers the shortest mate via the larger magnitude. The entry stores
   the exact node-relative score +-(MATE - d): the distance d is a property of
   the POSITION, not the path, so it is ply-independent and no ply needs to be
   stored. Translating back at the probe's own ply +-(MATE - d - P2) =
   +-(MATE - (P2 + d)) yields the correct root-relative score for that position
   reached at any ply. */
static Score tt_score_to_node(Score s, i16 ply) {   /* root-rel -> node-rel */
    if (s >= MATE - MAXPLY) return (Score)(s + ply);
    if (s <= -MATE + MAXPLY) return (Score)(s - ply);
    return s;
}

static Score tt_score_from_node(Score s, i16 ply) { /* node-rel -> root-rel */
    if (s >= MATE - MAXPLY) return (Score)(s - ply);
    if (s <= -MATE + MAXPLY) return (Score)(s + ply);
    return s;
}

/* empty the table (bench positions, xboard new/setboard) */
void tt_clear(void) {
    i16 i;
    for (i = 0; i < TT_ENTRIES; i++) {
        tt_table[i].key = 0;
        tt_table[i].info = 0;
        tt_table[i].move = 0;
        tt_table[i].score = 0;
        tt_table[i].eval = 0;
        tt_table[i].pad = 0;
    }
}

/* probe: 1 on a hit. Fills the stored move/score/flag/depth. The stored
   node-relative mate score is translated back to root-relative at the caller's
   ply; bounds/ordering decisions are left to the caller. */
i16 tt_probe(Pos *p, i16 ply, u16 *move_out, Score *score_out,
             i16 *flag_out, i16 *depth_out) {
    TTEnt *e = &tt_table[tt_index(p->sig)];

    PCOUNT(c_tt_probe);

    if (e->info != 0 && e->key == p->sig) {
        *move_out = e->move;
        *score_out = tt_score_from_node(e->score, ply);
        *flag_out = TT_FLAG(e->info);
        *depth_out = TT_DEPTH(e->info);
        return 1;
    }
    return 0;
}

/* store the node result. Replaces the slot iff the new depth is >= the stored
   one (equal depth replaces so an EXACT/bound update wins). Mate scores are
   converted from the search's root-relative to the node-relative encoding. */
void tt_store(Pos *p, u16 move, i16 depth, Score score, i16 flag, i16 ply) {
    TTEnt *e = &tt_table[tt_index(p->sig)];

    PCOUNT(c_tt_store);

    if (depth < TT_DEPTH(e->info)) return;         /* keep the deeper entry */
    if (depth > 63) depth = 63;
    score = tt_score_to_node(score, ply);
    e->key = p->sig;
    e->move = move;
    e->score = score;
    e->eval = 0;
    e->info = TT_INFO(depth, flag);
    e->pad = 0;
}

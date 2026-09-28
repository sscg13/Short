
/* Reversible non-pawn move table. Each slot stores piece/from/to in 16 bits.
   Reconstruct its FULL 64-bit key from the existing Zobrist table on lookup;
   this saves 64 KB without accepting truncated-key matches. */
#if defined(__WATCOMC__) && !defined(__386__)
#define REP_FAR _far
#else
#define REP_FAR
#endif
static u16 REP_FAR rep_moves[8192];
static i16 rep_table_ready;
static Sig rep_key(u16 packed) {
    i16 pc = packed >> 12;
    return zpsq[pc][packed & 63] ^ zpsq[pc][(packed >> 6) & 63] ^ zside_delta;
}
#define REP_KEY(i) rep_key(rep_moves[i])
static i16 rep_h1(Sig k) { return (u16)k & 8191; }
static i16 rep_h2(Sig k) { return (u16)((u32)k >> 16) & 8191; }

static i16 rep_step(i16 pc, i16 a, i16 b) {
    i16 dx = (b & 7) - (a & 7), dy = (b >> 4) - (a >> 4);
    i16 ax = abs(dx), ay = abs(dy), ty = TY(pc);
    if (ty == 2) return (ax == 1 && ay == 2) || (ax == 2 && ay == 1) ? b-a : 0;
    if (ty == 6) return ax <= 1 && ay <= 1 ? b-a : 0;
    if ((ty == 3 || ty == 5) && ax == ay)
        return (dx > 0 ? 1 : -1) + (dy > 0 ? 16 : -16);
    if ((ty == 4 || ty == 5) && (!dx || !dy))
        return dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 16 : -16);
    return 0;
}

static void rep_table_init(void) {
    i16 pc, a, b, slot, tries, count = 0;
    Sig key;
    u16 move, previous;
    rep_table_ready = 0;
    for (slot = 0; slot < 8192; ++slot) rep_moves[slot] = 0;
    for (pc = WN; pc <= BK; ++pc) {
        if (TY(pc) < 2 || TY(pc) > 6) continue;
        for (a = 0; a < 64; ++a) for (b = a + 1; b < 64; ++b) {
            if (!rep_step(pc, c2sq(a), c2sq(b))) continue;
            key = zpsq[pc][a] ^ zpsq[pc][b] ^ zside_delta;
            move = (u16)a | ((u16)b << 6) | ((u16)pc << 12);
            slot = rep_h1(key);
            for (tries = 0; tries < 8192; ++tries) {
                previous = rep_moves[slot];
                rep_moves[slot] = move;
                if (!previous) break;
                move = previous; key = rep_key(move);
                slot = slot == rep_h1(key) ? rep_h2(key) : rep_h1(key);
            }
            if (tries == 8192) return; /* deterministic failure: disable, never hang */
            ++count;
        }
    }
    rep_table_ready = count == 3668;
}

/* Return the legal reversible move to target, or zero. Validate the complete
   resulting key, rights and king safety rather than relying on geometry alone.
   This make/undo is reached only after both the key and history match. */
u16 repetition_move(Pos *p, Sig target) {
    Sig delta = p->sig ^ target;
    i16 slot, a, b, pc, step, sq, us;
    u16 packed, move;
    Undo undo;
    i16 legal;
    if (!rep_table_ready) return 0;
    slot = rep_h1(delta);
    if (!rep_moves[slot] || REP_KEY(slot) != delta) {
        slot = rep_h2(delta);
        if (!rep_moves[slot] || REP_KEY(slot) != delta) return 0;
    }
    packed = rep_moves[slot];
    a = mfrom(packed); b = mto(packed); pc = packed >> 12;
    if (CO(pc) != (p->side ? 8 : 0)) return 0;
    if (p->board[a] != pc) { sq = a; a = b; b = sq; }
    if (p->board[a] != pc || p->board[b] != EMPTY) return 0;
    step = rep_step(pc, a, b);
    if (!step) return 0;
    for (sq = a + step; sq != b; sq += step)
        if (p->board[sq]) return 0;
    move = MK(b, a, 0, 0); us = p->side;
    do_make(p, move, &undo);
    legal = p->sig == target && !is_attacked(p, p->ks[us], p->side);
    undo_move(p, move, &undo);
    return legal ? move : 0;
}

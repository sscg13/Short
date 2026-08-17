// convert_binpack.cpp - C++ port of convert_binpack.py (the Python version is
// authoritative for the format and filter semantics; this must produce the
// same output records byte-for-byte for the same input/flags/seed).
//
//     convert_binpack <input.binpack> <output.records> [flags]
//
// Reads the Stockfish/nnue-pytorch "binpack" format (BINP blocks; see the
// Python file's header comment for the full format spec), applies the hard and
// AOT-translated filters, and writes fixed 40-byte records under a SH01 header.
//
// Determinism: the RNG is a CPython-compatible MT19937 (init_by_array for an
// integer seed + genrand_res53 for random()), so with --seed the probabilistic
// filters make the same decisions as the Python version. Every deterministic
// filter is pure integer/bitboard logic and is byte-identical by construction.
// The one caveat is wld_filtered: its threshold uses the platform exp(), which
// can differ from CPython's math.exp by 1 ULP and flip a borderline draw.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cinttypes>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// ---------------------------------------------------------------------------
// Constants (mirror the Python file)
// ---------------------------------------------------------------------------
static constexpr int PIECE_NONE = 12;
// internal piece codes: 0=WP 1=BP 2=WN 3=BN 4=WB 5=BB 6=WR 7=BR 8=WQ 9=BQ
//                        10=WK 11=BK (chess.h Piece ordinal: (type<<1)|color)
// piece type: 0=P 1=N 2=B 3=R 4=Q 5=K
static constexpr int PT[13] = {0,0,1,1,2,2,3,3,4,4,5,5,6};
static constexpr int COLOR[13] = {0,1,0,1,0,1,0,1,0,1,0,1,0};
static constexpr int OUT[13] = {1,7,2,8,3,9,4,10,5,11,6,12,0};

// castling bits: 1=WK 2=WQ 4=BK 8=BQ
static constexpr int PRESERVED[64] = {
    0xD,0xF,0xF,0xF,0xC,0xF,0xF,0xE,   // rank1: a1 clears WQ, e1 clears White
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    0x7,0xF,0xF,0xF,0x3,0xF,0xF,0xB,   // rank8: a8 clears BQ, e8 clears Black
};
static constexpr int MOVE_NORMAL = 0, MOVE_PROMO = 1, MOVE_CASTLE = 2, MOVE_EP = 3;

static constexpr int VALUE_NONE = 32002;
static constexpr double MAX_PC_SKIP_RATE = 0.975;
static constexpr double DEFAULT_PC_Y[5] = {0.0, 0.4, 1.0, 1.0, 0.75};
static constexpr double WIN_RATE_AS[4] = {-3.68389304, 30.07065921, -60.52878723, 149.53378557};
static constexpr double WIN_RATE_BS[4] = {-2.0181857, 15.85685038, -29.83452023, 47.59078827};
static constexpr int SIMPLE_EVAL_VAL[5] = {208, 781, 825, 1276, 2538};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static int used_bits_safe(int value) {
    if (value <= 1) return 0;
    return 32 - __builtin_clz((unsigned)(value - 1));
}

static int unsigned_to_signed(uint16_t r) {
    r = (uint16_t)(((r << 15) | (r >> 1)) & 0xFFFF);
    if (r & 0x8000) r ^= 0x7FFF;
    return r >= 0x8000 ? (int)r - 0x10000 : (int)r;
}

static int16_t to_i16(int x) { return (int16_t)(x & 0xFFFF); }

static int nth_set_bit(uint64_t bits, int n) {
    for (int i = 0; i < n; i++) bits &= bits - 1;
    return __builtin_ctzll(bits);
}

// ---------------------------------------------------------------------------
// Attack tables (identical construction to the Python file)
// ---------------------------------------------------------------------------
static uint64_t PAWN_ATT[64][2], KNIGHT_ATT[64], KING_ATT[64];

static int sqf(int f, int r) { return f + 8 * r; }

static void build_attack_tables() {
    for (int sq = 0; sq < 64; sq++) {
        int f = sq & 7, r = sq >> 3;
        if (r < 7) {
            if (f < 7) PAWN_ATT[sq][0] |= 1ULL << (sq + 9);
            if (f > 0) PAWN_ATT[sq][0] |= 1ULL << (sq + 7);
        }
        if (r > 0) {
            if (f < 7) PAWN_ATT[sq][1] |= 1ULL << (sq - 7);
            if (f > 0) PAWN_ATT[sq][1] |= 1ULL << (sq - 9);
        }
        const int kd[8][2] = {{-1,-2},{1,-2},{-2,-1},{2,-1},{-2,1},{2,1},{-1,2},{1,2}};
        for (auto& d : kd) {
            int ff = f + d[0], rr = r + d[1];
            if (0 <= ff && ff < 8 && 0 <= rr && rr < 8) KNIGHT_ATT[sq] |= 1ULL << sqf(ff, rr);
        }
        const int kk[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
        for (auto& d : kk) {
            int ff = f + d[0], rr = r + d[1];
            if (0 <= ff && ff < 8 && 0 <= rr && rr < 8) KING_ATT[sq] |= 1ULL << sqf(ff, rr);
        }
    }
}

static uint64_t attacks_bb(int pt, int sq, uint64_t occupied) {
    if (pt == 1) return KNIGHT_ATT[sq];
    int f = sq & 7, r = sq >> 3;
    uint64_t bb = 0;
    static const int dirsB[4][2] = {{1,1},{-1,1},{1,-1},{-1,-1}};
    static const int dirsR[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};
    static const int dirsQ[8][2] = {{1,1},{-1,1},{1,-1},{-1,-1},{0,1},{1,0},{0,-1},{-1,0}};
    const int(*dirs)[2];
    int nd;
    if (pt == 2) { dirs = dirsB; nd = 4; }
    else if (pt == 3) { dirs = dirsR; nd = 4; }
    else { dirs = dirsQ; nd = 8; }
    for (int d = 0; d < nd; d++) {
        int ff = f, rr = r;
        for (;;) {
            ff += dirs[d][0]; rr += dirs[d][1];
            if (!(0 <= ff && ff < 8 && 0 <= rr && rr < 8)) break;
            int t = sqf(ff, rr);
            bb |= 1ULL << t;
            if (occupied & (1ULL << t)) break;
        }
    }
    return bb;
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------
struct Pos {
    uint8_t mailbox[64];
    int side = 0;
    int ep = -1;
    int castling = 0;
    int rule50 = 0;
    int ply = 0;
    unsigned char compressed[24] = {0};
    bool has_compressed = false;

    uint64_t pieces_of(int color) const {
        uint64_t bb = 0;
        for (int i = 0; i < 64; i++)
            if (mailbox[i] < 12 && COLOR[mailbox[i]] == color) bb |= 1ULL << i;
        return bb;
    }
    uint64_t pieces_of_type(int pt, int color) const {
        uint64_t bb = 0;
        for (int i = 0; i < 64; i++)
            if (PT[mailbox[i]] == pt && COLOR[mailbox[i]] == color) bb |= 1ULL << i;
        return bb;
    }
    int king_sq(int color) const {
        for (int i = 0; i < 64; i++)
            if (PT[mailbox[i]] == 5 && COLOR[mailbox[i]] == color) return i;
        return -1;
    }
};

static Pos decompress_position(const unsigned char* compressed) {
    uint64_t occupied = 0;
    for (int i = 0; i < 8; i++) occupied = (occupied << 8) | compressed[i];
    const unsigned char* state = compressed + 8;
    Pos p;
    for (int i = 0; i < 64; i++) p.mailbox[i] = PIECE_NONE;   // empty squares
    memcpy(p.compressed, compressed, 24);
    p.has_compressed = true;
    uint64_t b = occupied;
    int i = 0;
    while (b) {
        int sq = __builtin_ctzll(b);
        int nib = (state[i >> 1] >> (4 * (i & 1))) & 0xF;
        if (nib < 12) p.mailbox[sq] = (uint8_t)nib;
        else if (nib == 12) {
            if ((sq >> 3) == 3) { p.mailbox[sq] = 0; p.ep = sq - 8; }
            else { p.mailbox[sq] = 1; p.ep = sq + 8; }
        } else if (nib == 13) {
            p.mailbox[sq] = 6;
            if (sq == 0) p.castling |= 2;
            else if (sq == 7) p.castling |= 1;
        } else if (nib == 14) {
            p.mailbox[sq] = 7;
            if (sq == 56) p.castling |= 8;
            else if (sq == 63) p.castling |= 4;
        } else { p.mailbox[sq] = 11; p.side = 1; }
        b &= b - 1;
        i++;
    }
    return p;
}

static int compress_piece(const Pos& pos, int sq, int c) {
    int pt = PT[c];
    if (pt == 0) {
        if (pos.ep >= 0 && (sq & 7) == (pos.ep & 7)) {
            int rank = sq >> 3;
            if ((rank == 3 && pos.side == 1) || (rank == 4 && pos.side == 0)) return 12;
        }
        return c;
    }
    if (pt == 3) {
        if (c == 6) {
            if ((sq == 0 && pos.castling & 2) || (sq == 7 && pos.castling & 1)) return 13;
        } else if ((sq == 56 && pos.castling & 8) || (sq == 63 && pos.castling & 4)) return 14;
        return c;
    }
    if (pt == 5) return c == 10 ? 10 : (pos.side == 1 ? 15 : 11);
    return c;
}

static bool compress_position(const Pos& pos, unsigned char* out24) {
    uint64_t occupied = 0;
    for (int sq = 0; sq < 64; sq++)
        if (pos.mailbox[sq] < 12) occupied |= 1ULL << sq;
    unsigned char state[16] = {0};
    uint64_t b = occupied;
    int i = 0;
    while (b) {
        int sq = __builtin_ctzll(b);
        int nib = compress_piece(pos, sq, pos.mailbox[sq]);
        if (i & 1) state[i >> 1] |= (uint8_t)(nib << 4);
        else state[i >> 1] |= (uint8_t)nib;
        b &= b - 1;
        i++;
    }
    for (int k = 0; k < 8; k++) out24[k] = (unsigned char)((occupied >> (8 * (7 - k))) & 0xFF);
    memcpy(out24 + 8, state, 16);
    return true;
}

// ---------------------------------------------------------------------------
// Attack/check helpers (chess.h semantics)
// ---------------------------------------------------------------------------
static bool is_attacked_by_slider(int sq, uint64_t bishops, uint64_t rooks, uint64_t queens, uint64_t occupied) {
    if (attacks_bb(2, sq, occupied) & (bishops | queens)) return true;
    return (attacks_bb(3, sq, occupied) & (rooks | queens)) != 0;
}

static bool sq_attacked_by(const Pos& pos, int sq, int by) {
    uint64_t pawn_p = pos.pieces_of_type(0, by);
    if (by == 0) {
        if ((sq >= 9 && (pawn_p >> (sq - 9)) & 1) || (sq >= 7 && (pawn_p >> (sq - 7)) & 1)) return true;
    } else {
        if ((sq < 55 && (pawn_p >> (sq + 9)) & 1) || (sq < 57 && (pawn_p >> (sq + 7)) & 1)) return true;
    }
    if (KNIGHT_ATT[sq] & pos.pieces_of_type(1, by)) return true;
    if (KING_ATT[sq] & pos.pieces_of_type(5, by)) return true;
    uint64_t bishops = pos.pieces_of_type(2, by);
    uint64_t rooks = pos.pieces_of_type(3, by);
    uint64_t queens = pos.pieces_of_type(4, by);
    uint64_t occupied = pos.pieces_of(0) | pos.pieces_of(1);
    return is_attacked_by_slider(sq, bishops, rooks, queens, occupied);
}

static bool is_in_check(const Pos& pos) {
    int ksq = pos.king_sq(pos.side);
    return ksq >= 0 && sq_attacked_by(pos, ksq, 1 - pos.side);
}

static int simple_eval(const Pos& pos) {
    int s = 0;
    for (int i = 0; i < 64; i++) {
        int c = pos.mailbox[i];
        if (c < 12 && PT[c] < 5) s += (COLOR[c] == 0 ? 1 : -1) * SIMPLE_EVAL_VAL[PT[c]];
    }
    return pos.side == 0 ? s : -s;
}

// ---------------------------------------------------------------------------
// Bit reader + move decode (binpack.h PackedMoveScoreListReader)
// ---------------------------------------------------------------------------
struct BitReader {
    const unsigned char* data;
    int offset;
    int start;
    int bits_left;
    BitReader(const unsigned char* d, int off) : data(d), offset(off), start(off), bits_left(8) {}
    int extract(int count) {
        if (count == 0) return 0;
        if (bits_left == 0) { offset++; bits_left = 8; }
        int byte = (data[offset] << (8 - bits_left)) & 0xFF;
        int bits = byte >> (8 - count);
        if (count > bits_left) {
            int spill = count - bits_left;
            bits |= data[offset + 1] >> (8 - spill);
            bits_left += 8;
            offset++;
        }
        bits_left -= count;
        return bits & 0xFF;
    }
    int extract_vle16(int block_size = 4) {
        int mask = (1 << block_size) - 1;
        int v = 0, shift = 0;
        for (;;) {
            int block = extract(block_size + 1);
            v |= (block & mask) << shift;
            if (!(block >> block_size)) break;
            shift += block_size;
        }
        return v;
    }
    int num_read_bytes() const { return (offset - start) + (bits_left == 8 ? 0 : 1); }
};

struct Move { int from, to, mtype, promo; };

static Move decode_move_raw(uint16_t move_raw) {
    int mtype = move_raw >> 14;
    int from_sq = (move_raw >> 8) & 0x3F;
    int to_sq = (move_raw >> 2) & 0x3F;
    if (mtype == MOVE_PROMO) {
        int prom_idx = move_raw & 0x3;
        int color = (to_sq >> 3) == 0 ? 1 : 0;
        return {from_sq, to_sq, mtype, ((1 + prom_idx) << 1) | color};
    }
    return {from_sq, to_sq, mtype, -1};
}

static Move decode_movelist_move(const Pos& pos, BitReader& br, int last_score, int& score_out) {
    int side = pos.side;
    uint64_t our = pos.pieces_of(side);
    uint64_t their = pos.pieces_of(1 - side);
    uint64_t occupied = our | their;

    int piece_id = br.extract(used_bits_safe(__builtin_popcountll(our)));
    int from_sq = nth_set_bit(our, piece_id);
    int pt = PT[pos.mailbox[from_sq]];

    Move m;
    if (pt == 0) { // pawn
        int prom_rank = side == 0 ? 6 : 1;
        int start_rank = side == 0 ? 1 : 6;
        int fwd = side == 0 ? 8 : -8;
        uint64_t attack_targets = their;
        if (pos.ep >= 0) attack_targets |= 1ULL << pos.ep;
        uint64_t destinations = PAWN_ATT[from_sq][side] & attack_targets;
        int sq_forward = from_sq + fwd;
        if (!(occupied & (1ULL << sq_forward))) {
            destinations |= 1ULL << sq_forward;
            if ((from_sq >> 3) == start_rank && !(occupied & (1ULL << (sq_forward + fwd))))
                destinations |= 1ULL << (sq_forward + fwd);
        }
        int n_dest = __builtin_popcountll(destinations);
        if ((from_sq >> 3) == prom_rank) {
            int move_id = br.extract(used_bits_safe(n_dest * 4));
            int prom_idx = move_id % 4;
            int to_sq = nth_set_bit(destinations, move_id / 4);
            m = {from_sq, to_sq, MOVE_PROMO, ((1 + prom_idx) << 1) | side};
        } else {
            int move_id = br.extract(used_bits_safe(n_dest));
            int to_sq = nth_set_bit(destinations, move_id);
            m = {from_sq, to_sq, to_sq == pos.ep ? MOVE_EP : MOVE_NORMAL, -1};
        }
    } else if (pt == 5) { // king
        int our_castling = pos.castling & (side == 0 ? 0x3 : 0xC);
        uint64_t attacks = KING_ATT[from_sq] & ~our;
        int n_att = __builtin_popcountll(attacks);
        int n_cast = __builtin_popcountll(our_castling);
        int move_id = br.extract(used_bits_safe(n_att + n_cast));
        if (move_id >= n_att) {
            int idx = move_id - n_att;
            int long_right = side == 0 ? 2 : 8;
            bool long_castle = (idx == 0) && (our_castling & long_right);
            int to_sq = (side == 0 && long_castle) ? 0 : (long_castle ? 56 : (side == 0 ? 7 : 63));
            m = {from_sq, to_sq, MOVE_CASTLE, -1};
        } else {
            int to_sq = nth_set_bit(attacks, move_id);
            m = {from_sq, to_sq, MOVE_NORMAL, -1};
        }
    } else { // N/B/R/Q
        uint64_t attacks = attacks_bb(pt, from_sq, occupied) & ~our;
        int move_id = br.extract(used_bits_safe(__builtin_popcountll(attacks)));
        int to_sq = nth_set_bit(attacks, move_id);
        m = {from_sq, to_sq, MOVE_NORMAL, -1};
    }
    score_out = to_i16(last_score + unsigned_to_signed((uint16_t)br.extract_vle16(4)));
    return m;
}

static void nullify_ep(Pos& pos) {
    int ep = pos.ep;
    if (ep < 0) return;
    int side = pos.side;
    uint64_t cap_pawns = 0;
    for (int sq = 0; sq < 64; sq++)
        if (PT[pos.mailbox[sq]] == 0 && COLOR[pos.mailbox[sq]] == side && (PAWN_ATT[sq][side] >> ep) & 1)
            cap_pawns |= 1ULL << sq;
    if (cap_pawns == 0) { pos.ep = -1; return; }
    int fwd = side == 0 ? 8 : -8;
    int sqf_ = ep + fwd;
    if (!(0 <= sqf_ && sqf_ < 64) || pos.mailbox[sqf_] != PIECE_NONE) { pos.ep = -1; return; }
    int sqb = ep - fwd;
    if (!(0 <= sqb && sqb < 64) || pos.mailbox[sqb] != (uint8_t)(1 - side)) { pos.ep = -1; return; }
    uint64_t bishops = pos.pieces_of_type(2, 1 - side);
    uint64_t rooks = pos.pieces_of_type(3, 1 - side);
    uint64_t queens = pos.pieces_of_type(4, 1 - side);
    int ksq = pos.king_sq(side);
    if (ksq < 0) { pos.ep = -1; return; }
    uint64_t relevant = bishops | rooks | queens;
    uint64_t pseudo = 0;
    static const int qdirs[8][2] = {{1,1},{-1,1},{1,-1},{-1,-1},{0,1},{1,0},{0,-1},{-1,0}};
    for (auto& d : qdirs) {
        int ff = ksq & 7, rr = ksq >> 3;
        for (;;) {
            ff += d[0]; rr += d[1];
            if (!(0 <= ff && ff < 8 && 0 <= rr && rr < 8)) break;
            pseudo |= 1ULL << sqf(ff, rr);
        }
    }
    if (!(relevant & pseudo)) return;
    int captured = ep - fwd;
    uint64_t allocc = pos.pieces_of(side) | pos.pieces_of(1 - side);
    for (int s = 0; s < 64; s++) {
        if ((cap_pawns >> s) & 1) {
            uint64_t occ = ((allocc ^ (1ULL << s)) | (1ULL << ep)) ^ (1ULL << captured);
            if (!is_attacked_by_slider(ksq, bishops, rooks, queens, occ)) return;
        }
    }
    pos.ep = -1;
}

static void do_move(Pos& pos, Move mv) {
    int side = pos.side;
    uint8_t* mailbox = pos.mailbox;
    int moved_pt = PT[mailbox[mv.from]];

    pos.ply += 1;
    pos.rule50 += 1;
    if (mv.mtype != MOVE_CASTLE && (moved_pt == 0 || mailbox[mv.to] != PIECE_NONE)) pos.rule50 = 0;
    pos.castling &= PRESERVED[mv.from];
    pos.castling &= PRESERVED[mv.to];
    pos.ep = -1;
    if (moved_pt == 0 && ((mv.to ^ mv.from) == 16)) pos.ep = (mv.to + mv.from) >> 1;

    if (mv.mtype == MOVE_NORMAL) {
        mailbox[mv.to] = mailbox[mv.from]; mailbox[mv.from] = PIECE_NONE;
    } else if (mv.mtype == MOVE_PROMO) {
        mailbox[mv.to] = (uint8_t)mv.promo; mailbox[mv.from] = PIECE_NONE;
    } else if (mv.mtype == MOVE_EP) {
        int fwd = side == 0 ? 8 : -8;
        int cap = mv.to - fwd;
        mailbox[mv.to] = mailbox[mv.from]; mailbox[mv.from] = PIECE_NONE;
        mailbox[cap] = PIECE_NONE;
    } else { // castle
        bool ct_short = (mv.to & 7) == 7;
        int rook_to, king_to;
        if (side == 0) { rook_to = ct_short ? 5 : 3; king_to = ct_short ? 6 : 2; }
        else { rook_to = ct_short ? 61 : 59; king_to = ct_short ? 62 : 58; }
        uint8_t rook = mailbox[mv.to];
        uint8_t king = mailbox[mv.from];
        mailbox[mv.to] = PIECE_NONE;
        mailbox[mv.from] = PIECE_NONE;
        mailbox[rook_to] = rook;
        mailbox[king_to] = king;
    }
    pos.side ^= 1;
    nullify_ep(pos);
}

// ---------------------------------------------------------------------------
// Entry unpacking + chunk iteration
// ---------------------------------------------------------------------------
static void unpack_entry(const unsigned char* e, Pos& pos, int& score, int& ply, int& result, Move& mv) {
    pos = decompress_position(e);
    pos.rule50 = (int)(((uint16_t)((e[30] << 8) | e[31])) & 0xFF);
    score = unsigned_to_signed((uint16_t)((e[26] << 8) | e[27]));
    int pr = (e[28] << 8) | e[29];
    ply = pr & 0x3FFF;
    result = unsigned_to_signed((uint16_t)(pr >> 14));
    pos.ply = ply;
    mv = decode_move_raw((uint16_t)((e[24] << 8) | e[25]));
}

template <typename Fn>
static void for_each_entry(const std::vector<unsigned char>& payload, Fn fn) {
    size_t n = payload.size();
    size_t offset = 0;
    while (offset + 34 <= n) {
        const unsigned char* entry = payload.data() + offset;
        int num_plies = (entry[32] << 8) | entry[33];
        offset += 34;

        Pos pos;
        int score, ply, result;
        Move mv;
        unpack_entry(entry, pos, score, ply, result, mv);
        fn(pos, score, ply, result, mv, false);

        if (num_plies == 0) continue;

        BitReader br(payload.data(), (int)offset);
        int last_score = to_i16(-score);
        for (int i = 0; i < num_plies; i++) {
            do_move(pos, mv);
            int ns;
            mv = decode_movelist_move(pos, br, last_score, ns);
            score = ns;
            ply += 1;
            result = -result;
            last_score = to_i16(-score);
            fn(pos, score, ply, result, mv, true);
        }
        offset += br.num_read_bytes();
    }
}

// ---------------------------------------------------------------------------
// Output record + FEN
// ---------------------------------------------------------------------------
static void make_record(const Pos& pos, int ply, int result, int score, unsigned char* out40) {
    for (int k = 0; k < 32; k++)
        out40[k] = (unsigned char)((OUT[pos.mailbox[2 * k + 1]] << 4) | OUT[pos.mailbox[2 * k]]);
    out40[32] = (unsigned char)pos.side;
    out40[33] = (unsigned char)pos.king_sq(0);
    out40[34] = (unsigned char)pos.king_sq(1);
    out40[35] = (unsigned char)pos.rule50;
    out40[36] = (unsigned char)(ply & 0xFF);
    out40[37] = (unsigned char)(result < 0 ? 0 : (result == 0 ? 1 : 2));
    out40[38] = (unsigned char)(score & 0xFF);
    out40[39] = (unsigned char)((score >> 8) & 0xFF);
}

static const char FEN_PIECE[] = "PpNnBbRrQqKkX";
static void to_fen(const Pos& pos, int ply, std::string& out) {
    std::string rows[8];
    for (int rank = 7; rank >= 0; rank--) {
        std::string row;
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int c = pos.mailbox[f + 8 * rank];
            if (c == PIECE_NONE) empty++;
            else {
                if (empty) { row += (char)('0' + empty); empty = 0; }
                row += FEN_PIECE[c];
            }
        }
        if (empty) row += (char)('0' + empty);
        out += row;
        if (rank) out += '/';
    }
    out += pos.side == 0 ? " w " : " b ";
    int cr = pos.castling;
    std::string cast;
    if (cr & 1) cast += "K";
    if (cr & 2) cast += "Q";
    if (cr & 4) cast += "k";
    if (cr & 8) cast += "q";
    out += cast.empty() ? "-" : cast;
    out += " ";
    if (pos.ep < 0) out += "-";
    else { out += (char)('a' + (pos.ep & 7)); out += (char)('1' + (pos.ep >> 3)); }
    char tail[32];
    snprintf(tail, sizeof tail, " %d %d", pos.rule50, (ply + 1) / 2);
    out += tail;
}

// ---------------------------------------------------------------------------
// Filter helpers (AOT translations; see the Python file)
// ---------------------------------------------------------------------------
static void win_rate_model(int score, int ply, double& w, double& l, double& d) {
    double m = (double)std::min(240, ply) / 64.0;
    double a = ((WIN_RATE_AS[0] * m + WIN_RATE_AS[1]) * m + WIN_RATE_AS[2]) * m + WIN_RATE_AS[3];
    double b = ((WIN_RATE_BS[0] * m + WIN_RATE_BS[1]) * m + WIN_RATE_BS[2]) * m + WIN_RATE_BS[3];
    b *= 1.5;
    double x = std::max(-2000.0, std::min(2000.0, 100.0 * score / 208.0));
    w = 1.0 / (1.0 + std::exp((a - x) / b));
    l = 1.0 / (1.0 + std::exp((a + x) / b));
    d = 1.0 - w - l;
}

static double score_result_prob(int score, int ply, int result) {
    double w, l, d;
    win_rate_model(score, ply, w, l, d);
    if (result > 0) return w;
    if (result < 0) return l;
    return d;
}

static double pc_target_weight(int pc, const double y[5]) {
    if (pc <= 0) return y[0];
    if (pc >= 32) return y[4];
    int i = pc / 8;
    if (i > 3) i = 3;
    double x0 = i * 8.0;
    double t = (pc - x0) / 8.0;
    auto slope = [&](int idx) -> double {
        if (idx == 0) return y[1] - y[0];
        if (idx == 4) return y[4] - y[3];
        return (y[idx + 1] - y[idx - 1]) / 2.0;
    };
    double m0 = slope(i), m1 = slope(i + 1);
    double t2 = t * t, t3 = t2 * t;
    double h00 = 2 * t3 - 3 * t2 + 1;
    double h10 = t3 - 2 * t2 + t;
    double h01 = -2 * t3 + 3 * t2;
    double h11 = t3 - t2;
    return std::max(0.0, h00 * y[i] + h10 * m0 + h01 * y[i + 1] + h11 * m1);
}

static double interp_ply(double ply, const double x[4], const double y[4], int soft_early) {
    double p5x = (double)soft_early, p5y = 1.0;
    if (ply <= x[0]) return y[0];
    if (ply >= p5x) return p5y;
    for (int i = 0; i < 4; i++) {
        double x0 = x[i], y0 = y[i];
        double x1 = i < 3 ? x[i + 1] : p5x;
        double y1 = i < 3 ? y[i + 1] : p5y;
        if (x0 <= ply && ply <= x1) {
            if (x1 == x0) return y0;
            double t = (ply - x0) / (x1 - x0);
            return y0 + t * (y1 - y0);
        }
    }
    return 1.0;
}

// ---------------------------------------------------------------------------
// CPython-compatible MT19937 (random.Random(seed).random())
// ---------------------------------------------------------------------------
class PyRandom {
    uint32_t mt[624];
    int mti;
    void init_genrand(uint32_t s) {
        mt[0] = s & 0xFFFFFFFF;
        for (mti = 1; mti < 624; mti++)
            mt[mti] = (1812433253UL * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + (uint32_t)mti) & 0xFFFFFFFF;
    }
    uint32_t genrand_int32() {
        static const uint32_t mag01[2] = {0x0, 0x9908B0DFUL};
        if (mti >= 624) {
            int kk;
            for (kk = 0; kk < 227; kk++)
                mt[kk] = (mt[kk + 397] ^ (((mt[kk] & 0x80000000UL) | (mt[kk + 1] & 0x7fffffffUL)) >> 1) ^ mag01[mt[kk + 1] & 1]);
            for (; kk < 623; kk++)
                mt[kk] = (mt[kk - 227] ^ (((mt[kk] & 0x80000000UL) | (mt[kk + 1] & 0x7fffffffUL)) >> 1) ^ mag01[mt[kk + 1] & 1]);
            mt[623] = (mt[396] ^ (((mt[623] & 0x80000000UL) | (mt[0] & 0x7fffffffUL)) >> 1) ^ mag01[mt[0] & 1]);
            mti = 0;
        }
        uint32_t y = mt[mti++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9D2C5680UL;
        y ^= (y << 15) & 0xEFC60000UL;
        y ^= y >> 18;
        return y;
    }
public:
    void init_by_array(const std::vector<uint32_t>& key) {
        init_genrand(19650218UL);
        int i = 1, j = 0;
        int k = 624 > (int)key.size() ? 624 : (int)key.size();
        for (; k; k--) {
            mt[i] = (uint32_t)((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525UL)) + key[j] + j) & 0xFFFFFFFF;
            i++; j++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
            if (j >= (int)key.size()) j = 0;
        }
        for (k = 623; k; k--) {
            mt[i] = (uint32_t)((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941UL)) - i) & 0xFFFFFFFF;
            i++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
        }
        mt[0] = 0x80000000UL;
    }
    void seed_int(long long s) {
        std::vector<uint32_t> key;
        unsigned long long a = (unsigned long long)s;
        if (a == 0) key.push_back(0);
        while (a) { key.push_back((uint32_t)(a & 0xFFFFFFFF)); a >>= 32; }
        init_by_array(key);
    }
    double random() {
        uint32_t a = genrand_int32() >> 5;
        uint32_t b = genrand_int32() >> 6;
        return ((double)a * 67108864.0 + (double)b) * (1.0 / 9007199254740992.0);
    }
};

// ---------------------------------------------------------------------------
// CLI args
// ---------------------------------------------------------------------------
struct Args {
    std::string input, output;
    int max_abs_score = -1, min_ply = -1, max_ply = -1, min_rule50 = -1;
    bool skip_drawn = false;
    bool jit_defaults = false, filtered = false, wld_filtered = false, no_wld = false;
    int soft_early_skip = -1;
    double ply_x[4] = {0.0, 6.0, 10.0, 18.0};
    double ply_y[4] = {0.10, 0.15, 0.25, 0.75};
    int random_skip = -1, simple_eval_skip = -1, early_skip = -1;
    bool pc_spline = false;
    double pc_y[5] = {0.0, 0.4, 1.0, 1.0, 0.75};
    long long seed = 12345;
    int max_positions_per_game = -1, max_chunks = -1;
    bool report = false, check = false, quiet = false;
    int dump = 0;
};

static void usage(FILE* f) {
    fprintf(f,
        "usage: convert_binpack <input.binpack> <output.records> [options]\n"
        "  --max-abs-score N       drop |score| > N cp\n"
        "  --min-ply N / --max-ply N / --min-rule50 N\n"
        "  --skip-drawn\n"
        "  --jit-defaults           loader defaults: filtered, wld, soft-early 20, pc spline\n"
        "  --no-wld-filtered        disable wld_filtered even under --jit-defaults\n"
        "  --filtered               skip capture-move / in-check\n"
        "  --wld-filtered           skip with prob 1-score_result_prob\n"
        "  --soft-early-skip P [--ply-x1..--ply-y4]\n"
        "  --random-skip N          keep 1/(N+1)\n"
        "  --simple-eval-skip N     skip |simple_eval| < N\n"
        "  --early-skip N           skip ply <= N\n"
        "  --pc-spline Y0..Y4       two-pass piece-count resample\n"
        "  --seed N                 RNG seed (default 12345)\n"
        "  --max-positions-per-game N\n"
        "  --max-chunks N\n"
        "  --report / --check / --dump N / --quiet\n");
}

static int parse_double(const char* s, double& out) { return sscanf(s, "%lf", &out) == 1; }

// ---------------------------------------------------------------------------
// FilterBank (mirror the Python class)
// ---------------------------------------------------------------------------
struct FilterBank {
    const Args& a;
    PyRandom rng;
    int last_ply = -1;
    int last_score = VALUE_NONE;
    std::vector<double> early_lut;
    double pc_y[5];
    int64_t pc_seen[33] = {0};
    int64_t pc_total = 0;
    double pc_t[33] = {0}, pc_ratios[33] = {0}, pc_alpha = 1.0;
    bool pc_ready = false;

    FilterBank(const Args& args) : a(args) {
        rng.seed_int(a.seed);
        for (int i = 0; i < 5; i++) pc_y[i] = a.pc_y[i];
        if (a.soft_early_skip > 0) {
            for (int i = 0; i <= a.soft_early_skip; i++)
                early_lut.push_back(interp_ply((double)i, a.ply_x, a.ply_y, a.soft_early_skip));
        }
    }

    // returns a drop reason, or "" (empty) to keep. Same order as the JIT.
    std::string check(const Pos& pos, int score, int ply, int result, const Move& mv) {
        if (a.jit_defaults) {
            bool skip_phz = ply > last_ply && last_score != VALUE_NONE
                && abs(last_score) > 100 && result != 0 && score == 0;
            last_ply = ply;
            if (score == VALUE_NONE) return "value_none";
            if (skip_phz) return "placeholder_zero";
            last_score = score;
        }
        if (a.early_skip >= 0 && ply <= a.early_skip) return "early_skip";
        if (a.random_skip > 0) {
            if (rng.random() < (double)a.random_skip / (double)(a.random_skip + 1)) return "random_skip";
        }
        if (a.filtered) {
            bool cap_move = mv.mtype != MOVE_CASTLE && pos.mailbox[mv.to] < 12
                && COLOR[pos.mailbox[mv.to]] != COLOR[pos.mailbox[mv.from]];
            if (cap_move || is_in_check(pos)) return "filtered";
        }
        if (a.wld_filtered) {
            if (rng.random() > score_result_prob(score, ply, result)) return "wld_filtered";
        }
        if (a.simple_eval_skip > 0 && abs(simple_eval(pos)) < a.simple_eval_skip)
            return "simple_eval_skip";
        if (!early_lut.empty() && ply < a.soft_early_skip) {
            if (rng.random() > early_lut[ply]) return "soft_early";
        }
        return "";
    }

    void count_pc(const Pos& pos) {
        int pc = 0;
        for (int i = 0; i < 64; i++) if (pos.mailbox[i] < 12) pc++;
        if (0 <= pc && pc <= 32) { pc_seen[pc]++; pc_total++; }
    }

    void finalize_pc() {
        double t_total = 0;
        for (int pc = 0; pc < 33; pc++) { pc_t[pc] = pc_target_weight(pc, pc_y); t_total += pc_t[pc]; }
        double min_ratio = 0;
        bool have = false;
        for (int pc = 0; pc < 33; pc++) {
            pc_ratios[pc] = 0;
            if (pc_t[pc] > 0 && pc_seen[pc] > 0) {
                pc_ratios[pc] = ((double)pc_total * pc_t[pc]) / (t_total * (double)pc_seen[pc]);
                if (!have || pc_ratios[pc] < min_ratio) { min_ratio = pc_ratios[pc]; have = true; }
            }
        }
        pc_alpha = 1.0;
        if (have && min_ratio > 0.0) pc_alpha = (1.0 - MAX_PC_SKIP_RATE) / min_ratio;
        pc_ready = true;
    }

    bool pc_accept(const Pos& pos) {
        int pc = 0;
        for (int i = 0; i < 64; i++) if (pos.mailbox[i] < 12) pc++;
        if (pc < 0 || pc > 32) return false;
        if (pc_t[pc] <= 0 || pc_seen[pc] <= 0) return false;
        double p = pc_alpha * pc_ratios[pc];
        p = std::max(0.0, std::min(1.0, p));
        return rng.random() < p;
    }
};

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
struct Stats {
    std::vector<int64_t> chunk_sizes;
    int64_t records = 0, written = 0, filtered = 0;
    int64_t filter_by[16] = {0};   // indexed by FilterKey
    int64_t result[3] = {0};       // win, draw, loss (stm POV)
    std::map<int64_t, int64_t> score_hist, pc_hist;
    std::map<std::string, int64_t> ply_hist, rule_hist;
    int64_t bad_kings = 0, adjacent_kings = 0, roundtrip = 0, checked = 0;
};

enum FilterKey {
    FK_MAX_ABS_SCORE, FK_MIN_PLY, FK_MAX_PLY, FK_MIN_RULE50, FK_SKIP_DRAWN,
    FK_VALUE_NONE, FK_PLACEHOLDER_ZERO, FK_EARLY_SKIP, FK_RANDOM_SKIP,
    FK_FILTERED, FK_WLD_FILTERED, FK_SIMPLE_EVAL_SKIP, FK_SOFT_EARLY,
    FK_PC_SPLINE
};
static const char* FK_NAME[14] = {
    "max_abs_score", "min_ply", "max_ply", "min_rule50", "skip_drawn",
    "value_none", "placeholder_zero", "early_skip", "random_skip",
    "filtered", "wld_filtered", "simple_eval_skip", "soft_early",
    "pc_spline"
};

static std::string hard_drop(const Args& args, const Pos& pos, int score, int ply, int result, int& key) {
    key = -1;
    if (args.max_abs_score >= 0 && abs(score) > args.max_abs_score) { key = FK_MAX_ABS_SCORE; return FK_NAME[key]; }
    if (args.min_ply >= 0 && ply < args.min_ply) { key = FK_MIN_PLY; return FK_NAME[key]; }
    if (args.max_ply >= 0 && ply > args.max_ply) { key = FK_MAX_PLY; return FK_NAME[key]; }
    if (args.min_rule50 >= 0 && pos.rule50 < args.min_rule50) { key = FK_MIN_RULE50; return FK_NAME[key]; }
    if (args.skip_drawn && result == 0) { key = FK_SKIP_DRAWN; return FK_NAME[key]; }
    return "";
}

static int bucket(int sc) {
    int c = std::max(-32000, std::min(32000, sc));
    int q = c / 500;
    if (c < 0 && c % 500 != 0) q -= 1;
    return q * 500;
}
static std::string ply_label(int ply) {
    if (ply < 40) { char b[16]; snprintf(b, sizeof b, "%2d-%2d", ply / 10 * 10, ply / 10 * 10 + 9); return b; }
    return ">=40";
}
static std::string rule_label(int r) {
    if (r < 10) { char b[16]; snprintf(b, sizeof b, "%d-%d", r / 5 * 5, r / 5 * 5 + 4); return b; }
    return ">=10";
}
static bool adjacent(int a, int b) {
    return abs((a & 7) - (b & 7)) <= 1 && abs((a >> 3) - (b >> 3)) <= 1;
}

// ---------------------------------------------------------------------------
// Chunk iteration
// ---------------------------------------------------------------------------
static bool read_chunk(FILE* fh, std::vector<unsigned char>& payload, int max_chunks, int& chunk_no) {
    if (max_chunks >= 0 && chunk_no >= max_chunks) return false;
    char magic[4];
    if (fread(magic, 1, 4, fh) != 4) return false;
    if (memcmp(magic, "BINP", 4) != 0) {
        fprintf(stderr, "bad chunk magic %02X%02X%02X%02X at offset (old type-tagged format?)\n",
                (unsigned char)magic[0], (unsigned char)magic[1], (unsigned char)magic[2], (unsigned char)magic[3]);
        exit(1);
    }
    unsigned char szb[4];
    if (fread(szb, 1, 4, fh) != 4) { fprintf(stderr, "truncated chunk header\n"); exit(1); }
    uint32_t size = szb[0] | (szb[1] << 8) | (szb[2] << 16) | ((uint32_t)szb[3] << 24);
    if (size > 100u * 1024 * 1024) { fprintf(stderr, "chunk size %u too large (malformed?)\n", size); exit(1); }
    payload.resize(size);
    if (fread(payload.data(), 1, size, fh) != size) { fprintf(stderr, "truncated chunk\n"); exit(1); }
    chunk_no++;
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    build_attack_tables();

    Args a;
    std::vector<std::string> pos_args;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        auto next = [&](const char* name, const char*& out) {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", name); exit(1); }
            out = argv[++i];
        };
        if (s == "--max-abs-score") { const char* v; next("--max-abs-score", v); a.max_abs_score = atoi(v); }
        else if (s == "--min-ply") { const char* v; next("--min-ply", v); a.min_ply = atoi(v); }
        else if (s == "--max-ply") { const char* v; next("--max-ply", v); a.max_ply = atoi(v); }
        else if (s == "--min-rule50") { const char* v; next("--min-rule50", v); a.min_rule50 = atoi(v); }
        else if (s == "--skip-drawn") a.skip_drawn = true;
        else if (s == "--jit-defaults") a.jit_defaults = true;
        else if (s == "--no-wld-filtered") a.no_wld = true;
        else if (s == "--filtered") a.filtered = true;
        else if (s == "--wld-filtered") a.wld_filtered = true;
        else if (s == "--soft-early-skip") { const char* v; next("--soft-early-skip", v); a.soft_early_skip = atoi(v); }
        else if (s == "--ply-x1") { const char* v; next("--ply-x1", v); parse_double(v, a.ply_x[0]); }
        else if (s == "--ply-y1") { const char* v; next("--ply-y1", v); parse_double(v, a.ply_y[0]); }
        else if (s == "--ply-x2") { const char* v; next("--ply-x2", v); parse_double(v, a.ply_x[1]); }
        else if (s == "--ply-y2") { const char* v; next("--ply-y2", v); parse_double(v, a.ply_y[1]); }
        else if (s == "--ply-x3") { const char* v; next("--ply-x3", v); parse_double(v, a.ply_x[2]); }
        else if (s == "--ply-y3") { const char* v; next("--ply-y3", v); parse_double(v, a.ply_y[2]); }
        else if (s == "--ply-x4") { const char* v; next("--ply-x4", v); parse_double(v, a.ply_x[3]); }
        else if (s == "--ply-y4") { const char* v; next("--ply-y4", v); parse_double(v, a.ply_y[3]); }
        else if (s == "--random-skip") { const char* v; next("--random-skip", v); a.random_skip = atoi(v); }
        else if (s == "--simple-eval-skip") { const char* v; next("--simple-eval-skip", v); a.simple_eval_skip = atoi(v); }
        else if (s == "--early-skip") { const char* v; next("--early-skip", v); a.early_skip = atoi(v); }
        else if (s == "--pc-spline") {
            for (int k = 0; k < 5; k++) { const char* v; next("--pc-spline", v); parse_double(v, a.pc_y[k]); }
            a.pc_spline = true;
        }
        else if (s == "--seed") { const char* v; next("--seed", v); a.seed = atoll(v); }
        else if (s == "--max-positions-per-game") { const char* v; next("--max-positions-per-game", v); a.max_positions_per_game = atoi(v); }
        else if (s == "--max-chunks") { const char* v; next("--max-chunks", v); a.max_chunks = atoi(v); }
        else if (s == "--report") a.report = true;
        else if (s == "--check") a.check = true;
        else if (s == "--dump") { const char* v; next("--dump", v); a.dump = atoi(v); }
        else if (s == "--quiet") a.quiet = true;
        else if (s == "--help" || s == "-h") { usage(stdout); return 0; }
        else pos_args.push_back(s);
    }

    if (a.jit_defaults) {
        a.filtered = true;
        if (!a.no_wld) a.wld_filtered = true;
        if (a.soft_early_skip < 0) a.soft_early_skip = 20;
        if (!a.pc_spline) { a.pc_spline = true; }
    }

    if (pos_args.size() < 1) { usage(stderr); return 1; }
    a.input = pos_args[0];
    if (pos_args.size() >= 2) a.output = pos_args[1];

    bool do_checks = a.report || a.check;
    bool write_output = !a.output.empty();
    bool pc_mode = a.pc_spline;

    Stats st;

    // ---- pass 1 (pc mode): count piece counts, RNG aligned with pass 2 ----
    // fb1 stays alive: pass 2's pc_accept() draws from fb1's rng (Python's
    // closure keeps fb1 too), and pass 1's dummy draw keeps that rng's stream
    // position identical to the Python version.
    FilterBank* fb1 = nullptr;
    if (pc_mode) {
        fb1 = new FilterBank(a);
        FILE* fh = fopen(a.input.c_str(), "rb");
        if (!fh) { fprintf(stderr, "cannot open %s\n", a.input.c_str()); return 1; }
        std::vector<unsigned char> payload;
        int chunk_no = 0;
        while (read_chunk(fh, payload, a.max_chunks, chunk_no)) {
            for_each_entry(payload, [&](Pos& pos, int score, int ply, int result, Move& mv, bool) {
                int key;
                if (!hard_drop(a, pos, score, ply, result, key).empty()) return;
                if (fb1->check(pos, score, ply, result, mv).empty()) {
                    fb1->rng.random();       // = pass 2's pc_accept() draw
                    fb1->count_pc(pos);
                }
            });
        }
        fclose(fh);
        fb1->finalize_pc();
    }

    FILE* out = nullptr;
    if (write_output) {
        out = fopen(a.output.c_str(), "wb");
        if (!out) { fprintf(stderr, "cannot open %s\n", a.output.c_str()); return 1; }
        fwrite("SH01", 1, 4, out);
        unsigned char hdr[12] = {40,0,0,0, 0,0,0,0, 0,0,0,0};
        fwrite(hdr, 1, 12, out);
    }

    FilterBank fb(a);
    int chunk_no = 0;
    std::vector<unsigned char> payload;
    std::vector<std::string> dump_lines;

    // The per-game cap selects positions SPREAD ACROSS the game (not the
    // earliest N that pass): consecutive positions are one ply apart and
    // highly correlated, so the earliest N would bias the dataset toward one
    // game stage. Buffer each game's passing positions and emit N evenly
    // spaced at the game boundary.
    struct GBuf { unsigned char rec[40]; int pc; std::string dl; };
    std::vector<GBuf> game_buf;

    auto flush_game = [&]() {
        if (game_buf.empty()) return;
        int n = a.max_positions_per_game;
        if (n >= 0 && (int)game_buf.size() > n) {
            for (int i = 0; i < n; i++) {          // i*len/n is strictly increasing
                int idx = (int)((int64_t)i * game_buf.size() / n);
                GBuf& g = game_buf[idx];
                if (write_output) fwrite(g.rec, 1, 40, out);
                st.written++;
                st.pc_hist[g.pc]++;
                if (!g.dl.empty()) dump_lines.push_back(std::to_string(dump_lines.size()) + " " + g.dl);
            }
        } else {
            for (auto& g : game_buf) {
                if (write_output) fwrite(g.rec, 1, 40, out);
                st.written++;
                st.pc_hist[g.pc]++;
                if (!g.dl.empty()) dump_lines.push_back(std::to_string(dump_lines.size()) + " " + g.dl);
            }
        }
        game_buf.clear();
    };

    FILE* fh = fopen(a.input.c_str(), "rb");
    if (!fh) { fprintf(stderr, "cannot open %s\n", a.input.c_str()); return 1; }
    while (read_chunk(fh, payload, a.max_chunks, chunk_no)) {
        st.chunk_sizes.push_back(payload.size());
        int chunk_records = 0;
        for_each_entry(payload, [&](Pos& pos, int score, int ply, int result, Move& mv, bool is_cont) {
            if (!is_cont) flush_game();      // new chain = new game (chains never span chunks)
            st.records++;
            chunk_records++;
            st.result[result > 0 ? 0 : (result == 0 ? 1 : 2)]++;
            st.score_hist[bucket(score)]++;
            st.ply_hist[ply_label(ply)]++;
            st.rule_hist[rule_label(pos.rule50)]++;

            int key;
            std::string drop = hard_drop(a, pos, score, ply, result, key);
            if (drop.empty()) {
                std::string r = fb.check(pos, score, ply, result, mv);
                if (!r.empty()) drop = r;
                else if (fb1 && !fb1->pc_accept(pos)) drop = "pc_spline";
            }
            if (!drop.empty()) {
                st.filtered++;
                for (int fk = 0; fk < 14; fk++) if (drop == FK_NAME[fk]) { st.filter_by[fk]++; break; }
                return;
            }

            if (do_checks) {
                st.checked++;
                int wk = pos.king_sq(0), bk = pos.king_sq(1);
                if (wk < 0 || wk > 63 || bk < 0 || bk > 63 || wk == bk) st.bad_kings++;
                else if (adjacent(wk, bk)) st.adjacent_kings++;
                if (!is_cont && pos.has_compressed) {
                    unsigned char c24[24];
                    compress_position(pos, c24);
                    if (memcmp(c24, pos.compressed, 24) != 0) st.roundtrip++;
                }
            }

            // ---- buffer for the per-game cap (write at game end) ----
            GBuf g;
            if (a.dump > 0 && (int)dump_lines.size() < a.dump) {
                char line[256];
                std::string fen;
                to_fen(pos, ply, fen);
                std::string mailhex;
                for (int i = 0; i < 64; i++) { char b[4]; snprintf(b, sizeof b, "%02x", pos.mailbox[i]); mailhex += b; }
                snprintf(line, sizeof line, "%d %d %d %d %d %d %d %s %s",
                        score, ply, result, pos.rule50, pos.side,
                        pos.king_sq(0), pos.king_sq(1), mailhex.c_str(), fen.c_str());
                g.dl = line;
            }
            make_record(pos, ply, result, score, g.rec);
            int pc = 0;
            for (int i = 0; i < 64; i++) if (pos.mailbox[i] < 12) pc++;
            g.pc = pc;
            game_buf.push_back(g);
        });
        if (!a.quiet)
            fprintf(stderr, "chunk %d: %zu bytes, %d entries\n", (int)st.chunk_sizes.size(),
                    payload.size(), chunk_records);
    }
    fclose(fh);
    flush_game();          // the file can end mid-game (last chain)

    if (write_output) {
        fseek(out, 8, SEEK_SET);
        unsigned char cnt[4] = {(unsigned char)(st.written & 0xFF), (unsigned char)((st.written >> 8) & 0xFF),
                                (unsigned char)((st.written >> 16) & 0xFF), (unsigned char)((st.written >> 24) & 0xFF)};
        fwrite(cnt, 1, 4, out);
        fclose(out);
    }

    for (auto& l : dump_lines) printf("%s\n", l.c_str());

    if (a.report) {
        FILE* o = stderr;
        fprintf(o, "================================================================\n");
        fprintf(o, "BINPACK CONVERT REPORT\n");
        fprintf(o, "================================================================\n");
        int n_chunks = (int)st.chunk_sizes.size();
        int64_t total = 0;
        for (auto s : st.chunk_sizes) total += s;
        fprintf(o, "chunks                  : %d\n", n_chunks);
        if (n_chunks) {
            fprintf(o, "chunk bytes min/avg/max : %" PRId64 " / %" PRId64 " / %" PRId64 "\n",
                    *std::min_element(st.chunk_sizes.begin(), st.chunk_sizes.end()),
                    total / n_chunks, *std::max_element(st.chunk_sizes.begin(), st.chunk_sizes.end()));
            fprintf(o, "  (file bytes          : %" PRId64 ")\n", total + 8 * (int64_t)n_chunks);
        }
        fprintf(o, "total entries           : %" PRId64 "\n", st.records);
        fprintf(o, "  written records       : %" PRId64 "\n", st.written);
        if (st.filtered) {
            double pct = 100.0 * (double)st.filtered / (double)std::max<int64_t>(1, st.records);
            fprintf(o, "  filtered             : %" PRId64 " (%.2f%%)\n", st.filtered, pct);
            for (int fk = 0; fk < 15; fk++)
                if (st.filter_by[fk]) fprintf(o, "      %-16s %" PRId64 "\n", FK_NAME[fk], st.filter_by[fk]);
        }
        fprintf(o, "result (side-to-move POV): win %" PRId64 ", draw %" PRId64 ", loss %" PRId64 "\n",
                st.result[0], st.result[1], st.result[2]);
        fprintf(o, "score histogram (cp, 500-wide buckets, lower edge):\n");
        for (auto& kv : st.score_hist) fprintf(o, "  %6" PRId64 "  %" PRId64 "\n", kv.first, kv.second);
        fprintf(o, "ply histogram (0-based half-moves):\n");
        for (auto& kv : st.ply_hist) fprintf(o, "  %-6s %" PRId64 "\n", kv.first.c_str(), kv.second);
        fprintf(o, "rule50 histogram:\n");
        for (auto& kv : st.rule_hist) fprintf(o, "  %-6s %" PRId64 "\n", kv.first.c_str(), kv.second);
        if (!st.pc_hist.empty()) {
            double tot = 0;
            for (auto& kv : st.pc_hist) tot += (double)kv.second;
            fprintf(o, "piece-count histogram (written records, pct of total):\n");
            for (auto& kv : st.pc_hist) fprintf(o, "  %2" PRId64 "  %7" PRId64 "  %5.2f%%\n",
                                                 kv.first, kv.second, 100.0 * kv.second / tot);
        }
        if (st.checked) {
            fprintf(o, "sanity checks:\n");
            fprintf(o, "  checked              : %" PRId64 "\n", st.checked);
            fprintf(o, "  missing/dup kings    : %" PRId64 "\n", st.bad_kings);
            fprintf(o, "  adjacent kings       : %" PRId64 "\n", st.adjacent_kings);
            fprintf(o, "  compress round-trip  : %" PRId64 " mismatches\n", st.roundtrip);
        }
        fprintf(o, "================================================================\n");
    }
    return 0;
}

// Shared CPU trainer: engine-exact forward, QAT backpropagation and factored FD.
// PyTorch supplies flat buffers and AdamW; this core has no Torch dependency.

#include <immintrin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#include <intrin.h>
#endif

namespace short_trainer {

constexpr int FEATURES = 704;
constexpr int NET_VERSION = 5;
constexpr int HIDDEN = 64;
constexpr int PERSPECTIVES = 2;
constexpr int RECORD_SIZE = 40;
constexpr int HEADER_SIZE = 16;
constexpr int PARAM_COUNT = FEATURES * HIDDEN + HIDDEN + 2 * HIDDEN + 1;

// The engine's score contract: raw output >> 5 is centipawns, so one
// network output unit is 256 cp. Keep both loss sigmoid mappings at the
// original 256/256 calibration for the batch-size experiment.
constexpr float PREDICTED_SCORE_OFFSET = 256.0f;
constexpr float PREDICTED_SCORE_SCALE = 256.0f;
constexpr float TEACHER_SCORE_OFFSET = 256.0f;
constexpr float TEACHER_SCORE_SCALE = 256.0f;
constexpr float OUTPUT_SHIFT = 32.0f;  // raw network output -> centipawns

struct Record {
    uint8_t board[32];
    uint8_t stm;
    uint8_t white_king;
    uint8_t black_king;
    uint8_t rule50;
    uint8_t ply;
    uint8_t result;  // 0 loss, 1 draw, 2 win for the side to move
    int16_t score;  // centipawns from the side to move's perspective
};
static_assert(sizeof(Record) == RECORD_SIZE, "record layout must stay 40 bytes");

struct Net {
    std::vector<float> w1;   // [FEATURES][HIDDEN], feature-major
    std::vector<float> b1;   // [HIDDEN]
    std::vector<float> w2;   // [2][HIDDEN], stm row then nstm row
    float bias = 0.0f;

    Net() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}
};

struct QuantizedNet {
    // w1/b1 hold i8-quantized VALUES stored as i16 so the forward accumulates
    // in native i16 lanes without widening. Sums are bounded by
    // 33 * 127 = 4191 < 32767 (bias + MAX_ACTIVE features), so i16 can never
    // overflow and matches the engine's i16 pre-activation representation.
    std::vector<uint16_t> w1;  // [FEATURES][HIDDEN], feature-major
    std::vector<uint16_t> b1;  // [HIDDEN]
    std::vector<int8_t> w2;
    int16_t bias = 0;

    // Empty for ordinary ReLU² QAT. FD installs one or 64 response tables and
    // caches their quantized output products once per batch.
    int curve_count = 0;
    std::vector<int32_t> curves;
    std::vector<int32_t> terms;

    int curve_offset(int neuron) const {
        return curve_count == HIDDEN ? (neuron % HIDDEN) * 256 : 0;
    }

    int response(int neuron, int index) const {
        return curves.empty() ? index * index : curves[curve_offset(neuron) + index];
    }

    QuantizedNet() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}
};

static bool load_net(const std::string& path, Net& net) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    uint8_t header[12];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != 12 || std::memcmp(header, "NNUE", 4) != 0 ||
        (header[4] != NET_VERSION && header[4] != 2) || header[5] != 0 ||
        header[6] != 192 || header[7] != 2 ||
        header[8] != HIDDEN || header[9] != 0 || header[10] != 0 || header[11] != 0)
        return false;
    std::vector<uint8_t> bytes(FEATURES * HIDDEN + HIDDEN + 2 * HIDDEN);
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (file.gcount() != std::streamsize(bytes.size())) return false;
    auto signed_byte = [](uint8_t x) { return float(int8_t(x)); };
    size_t at = 0;
    for (float& x : net.w1) x = signed_byte(bytes[at++]);
    if (header[4] == 2)
        for (int rank = 1; rank < 8; ++rank)
            for (int file = 0; file < 4; ++file)
                std::copy_n(net.w1.begin() + (rank * 8 + file) * HIDDEN, HIDDEN,
                            net.w1.begin() + (rank * 4 + file) * HIDDEN);
    for (float& x : net.b1) x = signed_byte(bytes[at++]);
    for (float& x : net.w2) x = signed_byte(bytes[at++]);
    uint8_t bias_bytes[2];
    file.read(reinterpret_cast<char*>(bias_bytes), 2);
    if (file.gcount() != 2) return false;
    int bias = int(bias_bytes[0]) | (int(bias_bytes[1]) << 8);
    if (bias >= 32768) bias -= 65536;
    net.bias = float(bias);
    return true;
}

static int quant_i8(float x) {
    // Clamp before float->int conversion; trainer weights are finite.
    x = std::max(-128.0f, std::min(127.0f, x));
    int q = int(x);
    float fraction = x - float(q);
    return q + int(fraction >= 0.5f) - int(fraction <= -0.5f);
}

static int quant_i16(float x) {
    // Clamp before float->int conversion; trainer weights are finite.
    x = std::max(-32768.0f, std::min(32767.0f, x));
    int q = int(x);
    float fraction = x - float(q);
    return q + int(fraction >= 0.5f) - int(fraction <= -0.5f);
}

static int arithmetic_shift(int value, int shift) {
    return value >> shift;
}

// The production experiment targets this AVX2 machine and fixed N=64 shape.
static inline __m256i round8(__m256 values, float low, float high) {
    values = _mm256_max_ps(_mm256_set1_ps(low),
                          _mm256_min_ps(values, _mm256_set1_ps(high)));
    __m256i whole = _mm256_cvttps_epi32(values);
    __m256 fraction = _mm256_sub_ps(values, _mm256_cvtepi32_ps(whole));
    __m256i up = _mm256_castps_si256(_mm256_cmp_ps(
        fraction, _mm256_set1_ps(0.5f), _CMP_GE_OQ));
    __m256i down = _mm256_castps_si256(_mm256_cmp_ps(
        fraction, _mm256_set1_ps(-0.5f), _CMP_LE_OQ));
    return _mm256_add_epi32(_mm256_sub_epi32(whole, up), down);
}

static inline __m128i quant8_words(const float* values) {
    __m256i q = round8(_mm256_loadu_ps(values), -128.0f, 127.0f);
    return _mm_packs_epi32(_mm256_castsi256_si128(q),
                          _mm256_extracti128_si256(q, 1));
}

static QuantizedNet quantize_net(const Net& net) {
    static_assert(HIDDEN == 64 && FEATURES == 704 && PERSPECTIVES == 2,
                  "AVX2 experiment requires the fixed 704->2x64 architecture");
    QuantizedNet q;
    for (int i = 0; i < FEATURES * HIDDEN; i += 8)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(q.w1.data() + i),
                         quant8_words(net.w1.data() + i));
    for (int i = 0; i < HIDDEN; i += 8)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(q.b1.data() + i),
                         quant8_words(net.b1.data() + i));
    for (int i = 0; i < PERSPECTIVES * HIDDEN; i += 8) {
        __m128i bytes = _mm_packs_epi16(quant8_words(net.w2.data() + i),
                                       _mm_setzero_si128());
        _mm_storel_epi64(reinterpret_cast<__m128i*>(q.w2.data() + i), bytes);
    }
    q.bias = int16_t(quant_i16(net.bias));
    return q;
}


struct Gradients {
    std::vector<float> w1;
    std::vector<float> b1;
    std::vector<float> w2;
    float bias = 0.0f;
    std::vector<float> curves;

    Gradients() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}

    void zero() {
        std::fill(w1.begin(), w1.end(), 0.0f);
        std::fill(b1.begin(), b1.end(), 0.0f);
        std::fill(w2.begin(), w2.end(), 0.0f);
        bias = 0.0f;
        std::fill(curves.begin(), curves.end(), 0.0f);
    }
};

class RecordFile {
public:
    explicit RecordFile(const std::string& path) : path_(path), file_(path, std::ios::binary) {
        if (!file_) throw std::runtime_error("cannot open record file: " + path);
        uint8_t header[HEADER_SIZE];
        file_.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file_.gcount() != HEADER_SIZE || std::memcmp(header, "SH01", 4) != 0)
            throw std::runtime_error("invalid SH01 record header: " + path);
        uint32_t size = get_u32(header + 4);
        count_ = get_u32(header + 8);
        if (size != RECORD_SIZE || get_u32(header + 12) != 0)
            throw std::runtime_error("invalid SH01 record metadata: " + path);
        file_.seekg(0, std::ios::end);
        std::streamoff actual = file_.tellg();
        std::streamoff expected = HEADER_SIZE + std::streamoff(count_) * RECORD_SIZE;
        if (actual != expected) throw std::runtime_error("truncated record file: " + path);
    }

    uint32_t count() const { return count_; }

    void read(uint32_t first, uint32_t count, std::vector<Record>& out) {
        if (first > count_ || count > count_ - first)
            throw std::runtime_error("record batch outside file: " + path_);
        out.resize(count);
        file_.seekg(HEADER_SIZE + std::streamoff(first) * RECORD_SIZE);
        file_.read(reinterpret_cast<char*>(out.data()), std::streamsize(count) * RECORD_SIZE);
        if (file_.gcount() != std::streamsize(count) * RECORD_SIZE)
            throw std::runtime_error("short record batch: " + path_);
    }

private:
    static uint32_t get_u32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
               (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    std::string path_;
    std::ifstream file_;
    uint32_t count_ = 0;
};

struct ActiveFeatures {
    int rows[PERSPECTIVES][32];
    int count[PERSPECTIVES] = {0, 0};
};

static int square(const Record& r, int sq) {
    uint8_t packed = r.board[sq >> 1];
    return (sq & 1) ? (packed >> 4) : (packed & 15);
}

static int row_for_piece(int piece, int compact) {
    int type = piece & 7;
    bool enemy = (piece & 8) != 0;
    if (!enemy) {
        if (type == 6) return (compact >> 3) * 4 + (compact & 3);
        if (type == 1) {
            int rank = compact >> 3;
            return (rank < 1 || rank > 6) ? -1 : 32 + (rank - 1) * 8 + (compact & 7);
        }
        if (type >= 2 && type <= 5) return 80 + (type - 2) * 64 + compact;
    } else {
        if (type == 6) return 336 + compact;
        if (type == 1) {
            int rank = compact >> 3;
            return (rank < 1 || rank > 6) ? -1 : 400 + (rank - 1) * 8 + (compact & 7);
        }
        if (type >= 2 && type <= 5) return 448 + (type - 2) * 64 + compact;
    }
    return -1;
}

struct FeatureRowTable {
    int16_t row[16][64];

    FeatureRowTable() {
        for (int piece = 0; piece < 16; ++piece)
            for (int sq = 0; sq < 64; ++sq)
                row[piece][sq] = int16_t(row_for_piece(piece, sq));
    }
};

static const FeatureRowTable feature_row_table;

// Called only for a nonzero occupied-square mask.
static inline int trailing_zeroes(uint64_t value) {
#if defined(__clang__) || defined(__GNUC__)
    return int(__builtin_ctzll(value));
#elif defined(_MSC_VER) && defined(_M_X64)
    unsigned long index;
    _BitScanForward64(&index, value);
    return int(index);
#else
    int index = 0;
    while (!(value & 1)) {
        value >>= 1;
        ++index;
    }
    return index;
#endif
}

static inline void append_feature(ActiveFeatures& out, int& n0, int& n1,
                                  int record_piece, int sq,
                                  int mirror0, int mirror1) {
    bool black = record_piece >= 7;
    int type = black ? record_piece - 6 : record_piece;
    int compact0 = mirror0 ? (sq ^ 7) : sq;
    int row0 = feature_row_table.row[(black ? 8 : 0) | type][compact0];
    if (row0 >= 0) {
        if (n0 >= 32) throw std::runtime_error("too many active NNUE features");
        out.rows[0][n0++] = row0;
    }
    int compact1 = 63 - sq;
    if (mirror1) compact1 ^= 7;
    int row1 = feature_row_table.row[(black ? 0 : 8) | type][compact1];
    if (row1 >= 0) {
        if (n1 >= 32) throw std::runtime_error("too many active NNUE features");
        out.rows[1][n1++] = row1;
    }
}

static ActiveFeatures features(const Record& r) {
    ActiveFeatures out;
    int n0 = 0;
    int n1 = 0;
    int mirror0 = (r.white_king & 7) >= 4;
    int mirror1 = (7 - (r.black_king & 7)) >= 4;
    // SH01 packs two squares per byte. Collapse each nonempty nibble into
    // its low bit, then visit occupied squares in the original ascending
    // order so floating-point gradient accumulation remains identical.
    for (int block = 0; block < 4; ++block) {
        uint64_t packed;
        std::memcpy(&packed, r.board + block * 8, sizeof(packed));
        uint64_t occupied = (packed | (packed >> 1) | (packed >> 2) |
                             (packed >> 3)) & UINT64_C(0x1111111111111111);
        while (occupied) {
            int sq = block * 16 + (trailing_zeroes(occupied) >> 2);
            occupied &= occupied - 1;
            append_feature(out, n0, n1, square(r, sq), sq, mirror0, mirror1);
        }
    }
    out.count[0] = n0;
    out.count[1] = n1;
    return out;
}

struct Forward {
    float acc[PERSPECTIVES][HIDDEN];
    float act[PERSPECTIVES][HIDDEN];
    float raw = 0.0f;
    float score = 0.0f;
};

static Forward forward(const Record& r, const Net& net, const ActiveFeatures& f, bool qat,
                       const QuantizedNet* qnet = nullptr) {
    Forward out;
    if (qat) {
        if (!qnet) throw std::runtime_error("QAT forward requires quantized weights");
        for (int p = 0; p < PERSPECTIVES; ++p) {
            // Feature-outer accumulation over contiguous i16 rows: no widening
            // uops, and the 4-ymm accumulator is a plausible register resident.
            uint16_t acc[HIDDEN];
            for (int j = 0; j < HIDDEN; ++j) acc[j] = qnet->b1[j];
            for (int k = 0; k < f.count[p]; ++k) {
                const uint16_t* row = qnet->w1.data() + size_t(f.rows[p][k]) * HIDDEN;
                for (int j = 0; j < HIDDEN; ++j) acc[j] += row[j];
            }
            for (int j = 0; j < HIDDEN; ++j) {
                int a = int16_t(acc[j]);
                out.acc[p][j] = float(a);
                out.act[p][j] = float(std::max(0, std::min(255, a)));
            }
        }
        int stm = r.stm ? 1 : 0;
        int nstm = stm ^ 1;
        int raw = qnet->bias;
        if (qnet->curves.empty()) {
            for (int j = 0; j < HIDDEN; ++j) {
                int as = int(out.act[stm][j]);
                int an = int(out.act[nstm][j]);
                raw += arithmetic_shift(as * as * qnet->w2[j], 9);
                raw += arithmetic_shift(an * an * qnet->w2[HIDDEN + j], 9);
            }
        } else {
            const int stride = qnet->curve_count == HIDDEN ? 256 : 0;
            const int32_t* curve = qnet->curves.data();
            for (int j = 0; j < HIDDEN; ++j) {
                int as = int(out.act[stm][j]);
                int an = int(out.act[nstm][j]);
                raw += arithmetic_shift(curve[j * stride + as] * qnet->w2[j], 9);
                raw += arithmetic_shift(curve[j * stride + an] * qnet->w2[HIDDEN + j], 9);
            }
        }
        out.raw = float(raw);
        out.score = float(arithmetic_shift(raw, 5));
        return out;
    }
    for (int p = 0; p < PERSPECTIVES; ++p) {
        float acc[HIDDEN];
        for (int j = 0; j < HIDDEN; ++j) acc[j] = net.b1[j];
        for (int k = 0; k < f.count[p]; ++k) {
            const float* row = &net.w1[size_t(f.rows[p][k]) * HIDDEN];
            for (int j = 0; j < HIDDEN; ++j) acc[j] += row[j];
        }
        for (int j = 0; j < HIDDEN; ++j) {
            out.acc[p][j] = acc[j];
            out.act[p][j] = std::max(0.0f, std::min(255.0f, acc[j]));
        }
    }
    int stm = r.stm ? 1 : 0;
    int nstm = stm ^ 1;
    float raw_float = net.bias;
    for (int j = 0; j < HIDDEN; ++j) {
        int as = int(out.act[stm][j]);
        int an = int(out.act[nstm][j]);
        raw_float += float(as * as) * net.w2[j] / 512.0f;
        raw_float += float(an * an) * net.w2[HIDDEN + j] / 512.0f;
    }
    out.raw = raw_float;
    out.score = out.raw / OUTPUT_SHIFT;
    return out;
}

static float sigmoid(float x) {
    if (x >= 0.0f) {
        float e = std::exp(-x);
        return 1.0f / (1.0f + e);
    }
    float e = std::exp(x);
    return e / (1.0f + e);
}

struct ScoreLossEntry {
    float probability;
    float derivative;
};

static ScoreLossEntry score_loss_entry(float score, float offset, float scale) {
    float q = (score - offset) / scale;
    float qm = (-score - offset) / scale;
    float sq = sigmoid(q);
    float sqm = sigmoid(qm);
    ScoreLossEntry entry;
    entry.probability = 0.5f * (1.0f + sq - sqm);
    entry.derivative =
        0.5f / scale * (sq * (1.0f - sq) + sqm * (1.0f - sqm));
    return entry;
}

// Both record scores and the engine-exact QAT output are integer centipawns.
// Precompute separate prediction and teacher mappings for every i16 value, so
// the batch hot path performs no exp calls. The smooth, non-QAT gradient check
// retains the continuous formula, as its perturbed scores are fractional.
struct ScoreLossTable {
    ScoreLossEntry entries[65536];

    ScoreLossTable(float offset, float scale) {
        for (int score = -32768; score <= 32767; ++score)
            entries[score + 32768] = score_loss_entry(float(score), offset, scale);
    }
};

static const ScoreLossTable PREDICTED_SCORE_LOSS_TABLE(
    PREDICTED_SCORE_OFFSET, PREDICTED_SCORE_SCALE);
static const ScoreLossTable TEACHER_SCORE_LOSS_TABLE(
    TEACHER_SCORE_OFFSET, TEACHER_SCORE_SCALE);

static const ScoreLossEntry& integer_score_loss_entry(int score,
                                                       const ScoreLossTable& table) {
    return table.entries[score + 32768];
}

enum class TrainingMethod { Qat, FactoredFD };

struct LossParams {
    float lambda = 1.0f;  // 1 = score target, 0 = game-result target
    float pow_exp = 2.5f;
    TrainingMethod method = TrainingMethod::Qat;
    bool training = true;
    int curve_count = 0;  // 0 = fixed ReLU², 1 = shared, 64 = per neuron
    const float* curve_parameters = nullptr;
    bool freeze_curve = false;
    int accumulator_step = 4;
    int response_step = 256;
};

static void prepare_activation(QuantizedNet& net, const LossParams& params) {
    if (params.curve_count != 0 && params.curve_count != 1 && params.curve_count != HIDDEN)
        throw std::runtime_error("activation curve count must be 0, 1 or 64");
    if (params.method == TrainingMethod::Qat && params.curve_count)
        throw std::runtime_error("custom activations require factored-fd");
    if (params.accumulator_step < 1 || params.accumulator_step > 255 ||
        params.response_step < 1 || params.response_step > 65025)
        throw std::runtime_error("invalid finite-difference step");
    if (params.method != TrainingMethod::FactoredFD) return;
    net.curve_count = params.curve_count ? params.curve_count : 1;
    net.curves.resize(net.curve_count * 256);
    for (size_t a = 0; a < net.curves.size(); ++a) {
        float value = params.curve_count ? params.curve_parameters[a] : float(a * a);
        if (!std::isfinite(value) || value < -65025 || value > 65025 ||
            ((a % 256) == 0 && value != 0))
            throw std::runtime_error("invalid activation response or nonzero origin");
        net.curves[a] = int32_t(std::round(value));
    }
    if (!params.training) return;
    net.terms.resize(2 * HIDDEN * 256);
    for (int j = 0; j < 2 * HIDDEN; ++j)
        for (int a = 0; a < 256; ++a)
            net.terms[j * 256 + a] = arithmetic_shift(net.response(j, a) * int(net.w2[j]), 9);
}

// Accumulates the exact derivative of mean(abs(pt-qf)^pow_exp) into grads.
// Parameters are in engine units: w1/b1 are accumulator units, w2 is the
// i8-like x64 row unit, and bias is raw output units.
static float qat_process_record(const Record& record, const Net& net,
                            const LossParams& params, Gradients& grads, bool qat,
                            const QuantizedNet* qnet) {
    ActiveFeatures f = features(record);
    Forward y = forward(record, net, f, qat, qnet);
    ScoreLossEntry predicted;
    if (qat && y.score >= -32768.0f && y.score <= 32767.0f)
        predicted = integer_score_loss_entry(int(y.score), PREDICTED_SCORE_LOSS_TABLE);
    else
        predicted = score_loss_entry(y.score, PREDICTED_SCORE_OFFSET, PREDICTED_SCORE_SCALE);
    const ScoreLossEntry& teacher = integer_score_loss_entry(
        int(record.score), TEACHER_SCORE_LOSS_TABLE);
    float qf = predicted.probability;
    float pf = teacher.probability;
    float result = 0.5f * float(record.result);
    float target = params.lambda * pf + (1.0f - params.lambda) * result;
    float error = qf - target;
    float abs_error = std::fabs(error);
    float loss;
    if (abs_error == 0.0f) return 0.0f;

    float dloss_dq;
    if (params.pow_exp == 2.0f) {
        loss = abs_error * abs_error;
        dloss_dq = 2.0f * abs_error;
    } else if (params.pow_exp == 2.5f) {
        float ae_15 = abs_error * std::sqrt(abs_error);
        loss = abs_error * ae_15;
        dloss_dq = params.pow_exp * ae_15;
    } else {
        loss = std::pow(abs_error, params.pow_exp);
        dloss_dq = params.pow_exp * std::pow(abs_error, params.pow_exp - 1.0f);
    }
    if (error < 0.0f) dloss_dq = -dloss_dq;
    if (!params.training) return loss;
    float draw = dloss_dq * predicted.derivative / OUTPUT_SHIFT;
    grads.bias += draw;

    int stm = record.stm ? 1 : 0;
    const float* w2_float = net.w2.data();
    const int8_t* w2_q = qat && qnet ? qnet->w2.data() : nullptr;
    // Division by a power of two equals multiplication by its exact inverse,
    // so these hoists do not change the results beyond association order.
    const float inv512 = 1.0f / 512.0f;
    const float inv256 = 1.0f / 256.0f;
    float dacc[PERSPECTIVES][HIDDEN];
    for (int p = 0; p < PERSPECTIVES; ++p) {
        int w2_base = (p == stm) ? 0 : HIDDEN;
        const float* act = y.act[p];
        const float* acc = y.acc[p];
        float* da = dacc[p];
        float* gb1 = grads.b1.data();
        float* gw2r = grads.w2.data() + w2_base;
        // Take the qat select and i8->f32 widen out of the hot loop.
        float w2row[HIDDEN];
        if (w2_q) {
            const int8_t* wq = w2_q + w2_base;
            for (int j = 0; j < HIDDEN; ++j) w2row[j] = float(wq[j]);
        } else {
            const float* wf = w2_float + w2_base;
            for (int j = 0; j < HIDDEN; ++j) w2row[j] = wf[j];
        }
        const float* wr = w2row;
        for (int j = 0; j < HIDDEN; ++j) {
            float a = act[j];
            gw2r[j] += draw * a * a * inv512;
            bool live = (acc[j] > 0.0f) & (acc[j] < 255.0f);
            float d = draw * (a * wr[j]) * inv256;
            da[j] = live ? d : 0.0f;
            gb1[j] += da[j];
        }
    }
    // Pass 3 scatters dacc into the feature-major w1 gradient rows; each row
    // update is a short restricted copy-add the compiler can vectorize.
    for (int p = 0; p < PERSPECTIVES; ++p) {
        const float* da = dacc[p];
        const int* rows = f.rows[p];
        for (int k = 0; k < f.count[p]; ++k) {
            float* row = grads.w1.data() + size_t(rows[k]) * HIDDEN;
            for (int j = 0; j < HIDDEN; ++j) row[j] += da[j];
        }
    }
    return loss;
}

// A measured local secant through the actual integer activation/product map.
// Multiply each local discrete secant by the clean loss derivative. The
// linear head uses an STE for output rounding; no activation derivative or
// random perturbation is used.
static void factored_update(const Record& record, const ActiveFeatures& f,
                            const Forward& y, const QuantizedNet& net,
                            float draw, Gradients& grads, const LossParams& params) {
    alignas(32) float dacc[PERSPECTIVES][HIDDEN];
    const __m256i zero = _mm256_setzero_si256();
    const __m256i maximum = _mm256_set1_epi32(255);
    const __m256i step = _mm256_set1_epi32(params.accumulator_step);
    const __m256i offsets = _mm256_set_epi32(1792, 1536, 1280, 1024, 768, 512, 256, 0);
    const __m256 scale = _mm256_set1_ps(draw / float(2 * params.accumulator_step));
    const bool learn_curve = params.curve_count && !params.freeze_curve;
    const int curve_stride = net.curve_count == HIDDEN ? 256 : 0;
    const __m256i response_step = _mm256_set1_epi32(params.response_step);
    const __m256i low = _mm256_set1_epi32(-65025);
    const __m256i high = _mm256_set1_epi32(65025);
    const __m256 curve_scale = _mm256_set1_ps(draw / float(2 * params.response_step));
    for (int p = 0; p < PERSPECTIVES; ++p) {
        int base = p == int(record.stm) ? 0 : HIDDEN;
        for (int j = 0; j < HIDDEN; j += 8) {
            __m256i a = _mm256_cvttps_epi32(_mm256_loadu_ps(y.acc[p] + j));
            __m256i up = _mm256_min_epi32(maximum, _mm256_max_epi32(zero, _mm256_add_epi32(a, step)));
            __m256i down = _mm256_min_epi32(maximum, _mm256_max_epi32(zero, _mm256_sub_epi32(a, step)));
            const int32_t* terms = net.terms.data() + (base + j) * 256;
            __m256i diff = _mm256_sub_epi32(
                _mm256_i32gather_epi32(terms, _mm256_add_epi32(offsets, up), 4),
                _mm256_i32gather_epi32(terms, _mm256_add_epi32(offsets, down), 4));
            _mm256_store_ps(dacc[p] + j, _mm256_mul_ps(_mm256_cvtepi32_ps(diff), scale));
            if (learn_curve) {
                __m256i index = _mm256_min_epi32(maximum, _mm256_max_epi32(zero, a));
                __m256i curve_index = curve_stride ? _mm256_add_epi32(index, offsets) : index;
                __m256i value = _mm256_i32gather_epi32(net.curves.data() + j * curve_stride, curve_index, 4);
                __m256i cp = _mm256_min_epi32(high, _mm256_max_epi32(low, _mm256_add_epi32(value, response_step)));
                __m256i cm = _mm256_min_epi32(high, _mm256_max_epi32(low, _mm256_sub_epi32(value, response_step)));
                __m256i w = _mm256_cvtepi8_epi32(_mm_loadl_epi64(
                    reinterpret_cast<const __m128i*>(net.w2.data() + base + j)));
                __m256i cdiff = _mm256_sub_epi32(_mm256_srai_epi32(_mm256_mullo_epi32(cp, w), 9),
                                               _mm256_srai_epi32(_mm256_mullo_epi32(cm, w), 9));
                alignas(32) int32_t indices[8];
                alignas(32) float credit[8];
                _mm256_store_si256(reinterpret_cast<__m256i*>(indices), index);
                _mm256_store_ps(credit, _mm256_mul_ps(_mm256_cvtepi32_ps(cdiff), curve_scale));
                for (int lane = 0; lane < 8; ++lane)
                    if (indices[lane]) grads.curves[(j + lane) * curve_stride + indices[lane]] += credit[lane];
            }
        }
        for (int j = 0; j < HIDDEN; ++j) grads.b1[j] += dacc[p][j];
        for (int k = 0; k < f.count[p]; ++k) {
            float* row = grads.w1.data() + size_t(f.rows[p][k]) * HIDDEN;
            for (int j = 0; j < HIDDEN; ++j) row[j] += dacc[p][j];
        }
    }
}

static float process_record(const Record& record, const Net& net,
                            const LossParams& params, Gradients& grads, bool qat,
                            const QuantizedNet* qnet) {
    if (params.method == TrainingMethod::Qat)
        return qat_process_record(record, net, params, grads, qat, qnet);
    ActiveFeatures f = features(record);
    Forward y = forward(record, net, f, true, qnet);
    ScoreLossEntry predicted = (y.score >= -32768.0f && y.score <= 32767.0f)
        ? integer_score_loss_entry(int(y.score), PREDICTED_SCORE_LOSS_TABLE)
        : score_loss_entry(y.score, PREDICTED_SCORE_OFFSET, PREDICTED_SCORE_SCALE);
    float teacher = integer_score_loss_entry(int(record.score), TEACHER_SCORE_LOSS_TABLE).probability;
    float target = params.lambda * teacher +
                   (1.0f - params.lambda) * (0.5f * float(record.result));
    float error = predicted.probability - target;
    float ae = std::fabs(error);
    if (ae == 0.0f) return 0.0f;
    float loss, derivative;
    if (params.pow_exp == 2.0f) {
        loss = ae * ae;
        derivative = 2.0f * ae;
    } else if (params.pow_exp == 2.5f) {
        float ae15 = ae * std::sqrt(ae);
        loss = ae * ae15;
        derivative = 2.5f * ae15;
    } else {
        loss = std::pow(ae, params.pow_exp);
        derivative = params.pow_exp * std::pow(ae, params.pow_exp - 1.0f);
    }
    if (!params.training) return loss;
    if (error < 0.0f) derivative = -derivative;
    float draw = derivative * predicted.derivative / OUTPUT_SHIFT;
    grads.bias += draw;
    int stm = record.stm ? 1 : 0;
    const int stride = qnet->curve_count == HIDDEN ? 256 : 0;
    const int32_t* curve = qnet->curves.data();
    for (int p = 0; p < 2; ++p) {
        int base = (p == stm) ? 0 : 64;
        for (int j = 0; j < 64; ++j)
            grads.w2[base + j] += draw * float(curve[j * stride + int(y.act[p][j])]) * (1.0f / 512.0f);
    }

    factored_update(record, f, y, *qnet, draw, grads, params);
    return loss;
}

class WorkerPool {
public:
    ~WorkerPool() { stop(); }
    void shutdown() { stop(); }

    void set_threads(int count) {
        if (count < 1) throw std::runtime_error("thread count must be positive");
        if (count == int(workers_.size())) return;
        stop();
        stopping_ = false;
        local_grads_.resize(count);
        local_loss_.assign(count, 0.0f);
        uint64_t gen;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            gen = generation_;
        }
        // Pass the starting generation by value: workers never write
        // generation_, and no dispatch can happen until this function
        // returns, so every thread begins consistently behind the next
        // dispatch regardless of when its body actually starts running.
        for (int i = 0; i < count; ++i)
            workers_.emplace_back(&WorkerPool::worker, this, i, gen);
    }

    int thread_count() const { return int(workers_.size()); }

    void run(const Record* records, size_t count, const Net* net,
             const QuantizedNet* qnet, const LossParams* params, bool qat) {
        if (workers_.empty()) set_threads(1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            records_ = records;
            count_ = count;
            net_ = net;
            qnet_ = qnet;
            params_ = params;
            qat_ = qat;
            finished_ = 0;
            error_ = nullptr;
            ++generation_;
        }
        wake_.notify_all();
        std::unique_lock<std::mutex> lock(mutex_);
        complete_.wait(lock, [&] { return finished_ == int(workers_.size()); });
        if (error_) std::rethrow_exception(error_);
    }

    const std::vector<Gradients>& gradients() const { return local_grads_; }
    const std::vector<float>& losses() const { return local_loss_; }

private:
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            ++generation_;
        }
        wake_.notify_all();
        for (std::thread& worker : workers_)
            if (worker.joinable()) worker.join();
        workers_.clear();
        local_grads_.clear();
        local_loss_.clear();
        stopping_ = false;
    }

    void worker(int id, uint64_t seen) {
        for (;;) {
            const Record* records;
            size_t count;
            const Net* net;
            const QuantizedNet* qnet;
            const LossParams* params;
            bool qat;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_) return;
                seen = generation_;
                records = records_;
                count = count_;
                net = net_;
                qnet = qnet_;
                params = params_;
                qat = qat_;
            }
            Gradients& grads = local_grads_[id];
            grads.curves.resize(params->curve_count * 256);
            grads.zero();
            float loss = 0.0f;
            size_t begin = count * size_t(id) / workers_.size();
            size_t end = count * size_t(id + 1) / workers_.size();
            std::exception_ptr error;
            try {
                for (size_t i = begin; i < end; ++i)
                    loss += process_record(records[i], *net, *params, grads, qat, qnet);
            } catch (...) {
                error = std::current_exception();
            }
            local_loss_[id] = loss;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (error && !error_) error_ = error;
                if (++finished_ == int(workers_.size())) complete_.notify_one();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable complete_;
    std::vector<std::thread> workers_;
    std::vector<Gradients> local_grads_;
    std::vector<float> local_loss_;
    bool stopping_ = false;
    uint64_t generation_ = 0;
    int finished_ = 0;
    std::exception_ptr error_;
    const Record* records_ = nullptr;
    size_t count_ = 0;
    const Net* net_ = nullptr;
    const QuantizedNet* qnet_ = nullptr;
    const LossParams* params_ = nullptr;
    bool qat_ = false;
};

static WorkerPool worker_pool;
// The persistent pool belongs to this module. Serialize complete calls, including
// configuration, while the Torch boundary releases the GIL.
static std::mutex batch_mutex;

void set_threads(int count) {
    std::lock_guard<std::mutex> lock(batch_mutex);
    worker_pool.set_threads(count);
}

void shutdown() {
    std::lock_guard<std::mutex> lock(batch_mutex);
    worker_pool.shutdown();
}

static float process_batch_impl(const Record* records, size_t count, const Net& net,
                                const LossParams& params, Gradients& grads, bool qat) {
    grads.curves.resize(params.curve_count * 256);
    grads.zero();
    if (!count) return 0.0f;
    QuantizedNet qnet;
    const QuantizedNet* qnet_ptr = nullptr;
    if (qat) {
        qnet = quantize_net(net);
        prepare_activation(qnet, params);
        qnet_ptr = &qnet;
    }
    if (worker_pool.thread_count() == 0) worker_pool.set_threads(1);
    int thread_count = worker_pool.thread_count();
    if (thread_count <= 1) {
        float loss = 0.0f;
        for (size_t i = 0; i < count; ++i)
            loss += process_record(records[i], net, params, grads, qat, qnet_ptr);
        float inv_count = 1.0f / float(count);
        for (float& x : grads.w1) x *= inv_count;
        for (float& x : grads.b1) x *= inv_count;
        for (float& x : grads.w2) x *= inv_count;
        grads.bias *= inv_count;
        for (float& x : grads.curves) x *= inv_count;
        return loss * inv_count;
    }
    worker_pool.run(records, count, &net, qnet_ptr, &params, qat);
    const std::vector<Gradients>& local_grads = worker_pool.gradients();
    const std::vector<float>& local_loss = worker_pool.losses();
    float loss = 0.0f;
    float inv_count = 1.0f / float(count);
    for (int t = 0; t < thread_count; ++t) loss += local_loss[t];
    // Sum each parameter in worker order and normalize before writing it.
    // This preserves addition grouping while avoiding a destination pass
    // per worker followed by another full pass for normalization.
    for (size_t j = 0; j < grads.w1.size(); ++j) {
        float sum = 0.0f;
        for (int t = 0; t < thread_count; ++t) sum += local_grads[t].w1[j];
        grads.w1[j] = sum * inv_count;
    }
    for (size_t j = 0; j < grads.b1.size(); ++j) {
        float sum = 0.0f;
        for (int t = 0; t < thread_count; ++t) sum += local_grads[t].b1[j];
        grads.b1[j] = sum * inv_count;
    }
    for (size_t j = 0; j < grads.w2.size(); ++j) {
        float sum = 0.0f;
        for (int t = 0; t < thread_count; ++t) sum += local_grads[t].w2[j];
        grads.w2[j] = sum * inv_count;
    }
    for (int t = 0; t < thread_count; ++t) grads.bias += local_grads[t].bias;
    grads.bias *= inv_count;
    for (size_t a = 0; a < grads.curves.size(); ++a) {
        for (int t = 0; t < thread_count; ++t) grads.curves[a] += local_grads[t].curves[a];
        grads.curves[a] *= inv_count;
    }
    return loss * inv_count;
}

static float process_batch(const Record* records, size_t count, const Net& net,
                           const LossParams& params, Gradients& grads) {
    return process_batch_impl(records, count, net, params, grads, true);
}

static float process_batch_smooth(const Record* records, size_t count, const Net& net,
                                  const LossParams& params, Gradients& grads) {
    return process_batch_impl(records, count, net, params, grads, false);
}

// Flat-buffer entry point used by the Torch extension. Records are packed
// 40-byte rows without the SH01 header; indices are int64 row numbers. The
// initial wrapper copies the small parameter vector into the checked Net
// representation, while the large record tensor remains zero-copy.
float process_batch_flat(const uint8_t* records, size_t record_count,
                         const int64_t* indices, size_t batch_count,
                         const float* parameters, float* gradient,
                         const LossParams& params) {
    std::lock_guard<std::mutex> lock(batch_mutex);
    if (!indices && batch_count > record_count)
        throw std::runtime_error("batch count exceeds record tensor");
    if (params.curve_count != 0 && params.curve_count != 1 && params.curve_count != HIDDEN)
        throw std::runtime_error("activation curve count must be 0, 1 or 64");
    if (!std::isfinite(params.lambda) || params.lambda < 0 || params.lambda > 1 ||
        !std::isfinite(params.pow_exp) || params.pow_exp < 1)
        throw std::runtime_error("invalid loss parameters");
    const size_t parameter_count = PARAM_COUNT + params.curve_count * 256;
    for (size_t a = 0; a < parameter_count; ++a)
        if (!std::isfinite(parameters[a]))
            throw std::runtime_error("parameters must be finite");
    LossParams options = params;
    options.curve_parameters = parameters + PARAM_COUNT;
    Net net;
    std::memcpy(net.w1.data(), parameters, sizeof(float) * FEATURES * HIDDEN);
    std::memcpy(net.b1.data(), parameters + FEATURES * HIDDEN, sizeof(float) * HIDDEN);
    std::memcpy(net.w2.data(), parameters + FEATURES * HIDDEN + HIDDEN,
                sizeof(float) * 2 * HIDDEN);
    net.bias = parameters[PARAM_COUNT - 1];

    std::vector<Record> selected(batch_count);
    for (size_t i = 0; i < batch_count; ++i) {
        if (indices) {
            if (indices[i] < 0 || uint64_t(indices[i]) >= record_count)
                throw std::runtime_error("batch index outside record tensor");
            std::memcpy(&selected[i], records + size_t(indices[i]) * RECORD_SIZE, RECORD_SIZE);
        } else {
            std::memcpy(&selected[i], records + i * RECORD_SIZE, RECORD_SIZE);
        }
    }
    Gradients grads;
    float loss = process_batch(selected.data(), selected.size(), net, options, grads);
    std::memcpy(gradient, grads.w1.data(), sizeof(float) * FEATURES * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN, grads.b1.data(), sizeof(float) * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN + HIDDEN, grads.w2.data(),
                sizeof(float) * 2 * HIDDEN);
    gradient[PARAM_COUNT - 1] = grads.bias;
    if (!grads.curves.empty())
        std::memcpy(gradient + PARAM_COUNT, grads.curves.data(), grads.curves.size() * sizeof(float));
    return loss;
}

}  // namespace short_trainer

#ifdef NNUE_TRAINER_TEST
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: nnue_trainer_test file.records [... ]\n"
                             "       nnue_trainer_test --net net path.records\n");
        return 2;
    }
    try {
        if ((argc == 4 || argc == 5) && std::strcmp(argv[1], "--net") == 0) {
            short_trainer::Net net;
            if (!short_trainer::load_net(argv[2], net))
                throw std::runtime_error("cannot load single-output 704 net");
            short_trainer::RecordFile file(argv[3]);
            uint32_t count = argc == 5 ? uint32_t(std::stoul(argv[4])) : 1;
            count = std::min(count, file.count());
            std::vector<short_trainer::Record> batch;
            file.read(0, count, batch);
            short_trainer::QuantizedNet qnet = short_trainer::quantize_net(net);
            std::printf("records=%u checked=%u\n", file.count(), count);
            for (uint32_t i = 0; i < count; ++i) {
                short_trainer::Forward y = short_trainer::forward(
                    batch[i], net, short_trainer::features(batch[i]), true, &qnet);
                std::printf("%u raw=%g score=%g\n", i, y.raw, y.score);
            }
            return 0;
        }
        for (int arg = 1; arg < argc; ++arg) {
        short_trainer::RecordFile file(argv[arg]);
        if (file.count() == 0) throw std::runtime_error("empty record file");
        std::vector<short_trainer::Record> batch;
        file.read(0, std::min<uint32_t>(file.count(), 16), batch);
        std::vector<short_trainer::Record> tail;
        file.read(file.count() - std::min<uint32_t>(file.count(), 8),
                  std::min<uint32_t>(file.count(), 8), tail);
        auto validate = [](const std::vector<short_trainer::Record>& records) {
            for (const auto& r : records) {
                if (r.stm > 1 || r.white_king >= 64 || r.black_king >= 64 || r.result > 2)
                    throw std::runtime_error("record field outside its encoded range");
                for (uint8_t byte : r.board)
                    if ((byte & 15) > 12 || (byte >> 4) > 12)
                        throw std::runtime_error("piece nibble outside its encoded range");
            }
        };
        validate(batch);
        validate(tail);
        short_trainer::Net net;
        for (size_t i = 0; i < net.w1.size(); ++i)
            net.w1[i] = 0.01f * float(int(i % 7) - 3);
        for (int j = 0; j < short_trainer::HIDDEN; ++j) {
            net.b1[j] = 32.0f;
            net.w2[j] = 0.5f;
            net.w2[short_trainer::HIDDEN + j] = -0.25f;
        }
        short_trainer::Gradients grads;
        short_trainer::LossParams params;
        float loss = short_trainer::process_batch(batch.data(), batch.size(), net, params, grads);
        const float eps = 1.0e-3f;
        auto numeric = [&](float& value) {
            float saved = value;
            value = saved + eps;
            float plus = short_trainer::process_batch_smooth(batch.data(), 1, net, params, grads);
            value = saved - eps;
            float minus = short_trainer::process_batch_smooth(batch.data(), 1, net, params, grads);
            value = saved;
            return (plus - minus) / (2.0f * eps);
        };
        short_trainer::Gradients one_grad;
        float one_loss = short_trainer::process_batch_smooth(batch.data(), 1, net, params, one_grad);
        float bias_numeric = numeric(net.bias);
        int row = short_trainer::features(batch[0]).rows[0][0];
        float w1_numeric = numeric(net.w1[row * short_trainer::HIDDEN]);
        float w2_numeric = numeric(net.w2[0]);
        bool ok = std::fabs(bias_numeric - one_grad.bias) < 2.0e-4f &&
                  std::fabs(w1_numeric - one_grad.w1[row * short_trainer::HIDDEN]) < 2.0e-4f &&
                  std::fabs(w2_numeric - one_grad.w2[0]) < 2.0e-4f;
        std::printf("file=%s records=%u first=%zu tail=%zu loss=%.9g "
                    "bias_grad=%.9g gradcheck=%s\n", argv[arg], file.count(),
                    batch.size(), tail.size(), loss, grads.bias, ok ? "ok" : "FAIL");
        if (!ok) {
            std::fprintf(stderr, "numeric/analytic gradient mismatch: bias %.9g/%.9g w1 %.9g/%.9g w2 %.9g/%.9g\n",
                         bias_numeric, one_grad.bias, w1_numeric,
                         one_grad.w1[row * short_trainer::HIDDEN], w2_numeric, one_grad.w2[0]);
            return 1;
        }
        (void)one_loss;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
#endif

// nnue_trainer.cpp - record loader, eight-head v3 forward pass, and gradients.
//
// This is deliberately dependency-free.  The future pybind/torch wrapper can
// hand its parameter and gradient buffers to process_batch() without copying
// the record data or duplicating the feature/derivative logic.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace short_trainer {

constexpr int FEATURES = 704;
constexpr int HIDDEN = 64;
constexpr int PERSPECTIVES = 2;
constexpr int OUTPUT_BUCKETS = 8;
constexpr int OUTPUT_STRIDE = PERSPECTIVES * HIDDEN;
constexpr int RECORD_SIZE = 40;
constexpr int HEADER_SIZE = 16;
constexpr int PARAM_COUNT = FEATURES * HIDDEN + HIDDEN +
                            OUTPUT_BUCKETS * OUTPUT_STRIDE + OUTPUT_BUCKETS;

// The engine's score contract: raw output >> 5 is centipawns, so one
// network output unit is 256 cp.  Both calibration constants are powers of 2.
constexpr float SCORE_OFFSET = 256.0f;
constexpr float SCORE_SCALE = 256.0f;
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
    std::vector<float> w2;   // [OUTPUT_BUCKETS][2][HIDDEN], stm then nstm
    std::vector<float> bias; // [OUTPUT_BUCKETS]

    Net() : w1(FEATURES * HIDDEN), b1(HIDDEN),
            w2(OUTPUT_BUCKETS * OUTPUT_STRIDE), bias(OUTPUT_BUCKETS) {}
};

struct QuantizedNet {
    // w1/b1 hold i8-quantized VALUES stored as i16 so the forward accumulates
    // in native i16 lanes without widening. Sums are bounded by
    // 33 * 127 = 4191 < 32767 (bias + MAX_ACTIVE features), so i16 can never
    // overflow and matches the engine's i16 pre-activation representation.
    std::vector<uint16_t> w1;  // [FEATURES][HIDDEN], feature-major
    std::vector<uint16_t> b1;  // [HIDDEN]
    std::vector<int8_t> w2;
    std::vector<int16_t> bias;

    QuantizedNet() : w1(FEATURES * HIDDEN), b1(HIDDEN),
                     w2(OUTPUT_BUCKETS * OUTPUT_STRIDE), bias(OUTPUT_BUCKETS) {}
};

static bool load_net(const std::string& path, Net& net) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    uint8_t header[12];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != 12 || std::memcmp(header, "NNUE", 4) != 0 ||
        header[5] != 0 || header[6] != 192 || header[7] != 2 ||
        header[8] != HIDDEN || header[9] != 0)
        return false;
    int version = header[4];
    int buckets = (version == 2 && header[10] == 0 && header[11] == 0) ? 1 :
                  (version == 3 && header[10] == OUTPUT_BUCKETS && header[11] == 0)
                      ? OUTPUT_BUCKETS : 0;
    if (!buckets) return false;
    std::vector<uint8_t> bytes(FEATURES * HIDDEN + HIDDEN + buckets * OUTPUT_STRIDE);
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (file.gcount() != std::streamsize(bytes.size())) return false;
    auto signed_byte = [](uint8_t x) { return float(int8_t(x)); };
    size_t at = 0;
    for (float& x : net.w1) x = signed_byte(bytes[at++]);
    for (float& x : net.b1) x = signed_byte(bytes[at++]);
    if (buckets == OUTPUT_BUCKETS) {
        for (float& x : net.w2) x = signed_byte(bytes[at++]);
    } else {
        for (int i = 0; i < OUTPUT_STRIDE; ++i) {
            float x = signed_byte(bytes[at++]);
            for (int b = 0; b < OUTPUT_BUCKETS; ++b)
                net.w2[b * OUTPUT_STRIDE + i] = x;
        }
    }
    for (int b = 0; b < buckets; ++b) {
        uint8_t bias_bytes[2];
        file.read(reinterpret_cast<char*>(bias_bytes), 2);
        if (file.gcount() != 2) return false;
        int bias = int(bias_bytes[0]) | (int(bias_bytes[1]) << 8);
        if (bias >= 32768) bias -= 65536;
        if (buckets == 1) {
            for (int ob = 0; ob < OUTPUT_BUCKETS; ++ob) net.bias[ob] = float(bias);
        } else {
            net.bias[b] = float(bias);
        }
    }
    return true;
}

static int quant_i8(float x) {
    // All trainable values are kept inside their representable ranges.  A
    // cast truncates toward zero, so adding +/-0.5 implements lround's
    // round-half-away-from-zero rule without a scalar CRT call per weight.
    int q = int(x + (x >= 0.0f ? 0.5f : -0.5f));
    return std::max(-128, std::min(127, q));
}

static int quant_i16(float x) {
    int q = int(x + (x >= 0.0f ? 0.5f : -0.5f));
    return std::max(-32768, std::min(32767, q));
}

static int arithmetic_shift(int value, int shift) {
    return value >> shift;
}

static void quantize_net(const Net& net, QuantizedNet& q) {
    for (size_t i = 0; i < q.w1.size(); ++i) q.w1[i] = uint16_t(int16_t(quant_i8(net.w1[i])));
    for (size_t i = 0; i < q.b1.size(); ++i) q.b1[i] = uint16_t(int16_t(quant_i8(net.b1[i])));
    for (size_t i = 0; i < q.w2.size(); ++i) q.w2[i] = int8_t(quant_i8(net.w2[i]));
    for (size_t i = 0; i < q.bias.size(); ++i) q.bias[i] = int16_t(quant_i16(net.bias[i]));
}

static QuantizedNet quantize_net(const Net& net) {
    QuantizedNet q;
    quantize_net(net, q);
    return q;
}

struct Gradients {
    std::vector<float> w1;
    std::vector<float> b1;
    std::vector<float> w2;
    std::vector<float> bias;

    Gradients() : w1(FEATURES * HIDDEN), b1(HIDDEN),
                  w2(OUTPUT_BUCKETS * OUTPUT_STRIDE), bias(OUTPUT_BUCKETS) {}

    void zero() {
        std::fill(w1.begin(), w1.end(), 0.0f);
        std::fill(b1.begin(), b1.end(), 0.0f);
        std::fill(w2.begin(), w2.end(), 0.0f);
        std::fill(bias.begin(), bias.end(), 0.0f);
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
    int output_bucket = 0;
};

static int square(const Record& r, int sq) {
    uint8_t packed = r.board[sq >> 1];
    return (sq & 1) ? (packed >> 4) : (packed & 15);
}

static int row_for_piece(int piece, int compact) {
    int type = piece & 7;
    bool enemy = (piece & 8) != 0;
    if (!enemy) {
        if (type == 6) return compact;
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
            for (int square = 0; square < 64; ++square)
                row[piece][square] = int16_t(row_for_piece(piece, square));
    }
};

static const FeatureRowTable feature_row_table;

static inline int trailing_zeroes(uint64_t value) {
#if defined(__clang__) || defined(__GNUC__)
    return int(__builtin_ctzll(value));
#else
    unsigned long index;
    _BitScanForward64(&index, value);
    return int(index);
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
    int pieces = 0;
    int mirror0 = (r.white_king & 7) >= 4;
    int mirror1 = (7 - (r.black_king & 7)) >= 4;
    for (int block = 0; block < 4; ++block) {
        uint64_t packed;
        std::memcpy(&packed, r.board + block * 8, sizeof(packed));
        uint64_t occupied = (packed | (packed >> 1) | (packed >> 2) |
                             (packed >> 3)) & UINT64_C(0x1111111111111111);
        while (occupied) {
            int sq = block * 16 + (trailing_zeroes(occupied) >> 2);
            occupied &= occupied - 1;
            ++pieces;
            append_feature(out, n0, n1, square(r, sq), sq, mirror0, mirror1);
        }
    }
    out.count[0] = n0;
    out.count[1] = n1;
    out.output_bucket = std::max(0, std::min(OUTPUT_BUCKETS - 1, (pieces - 1) / 4));
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
    int output_base = f.output_bucket * OUTPUT_STRIDE;
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
        int raw = qnet->bias[f.output_bucket];
        for (int j = 0; j < HIDDEN; ++j) {
            int as = int(out.act[stm][j]);
            int an = int(out.act[nstm][j]);
            raw += arithmetic_shift(as * as * qnet->w2[output_base + j], 9);
            raw += arithmetic_shift(an * an * qnet->w2[output_base + HIDDEN + j], 9);
        }
        out.raw = float(raw);
        /* Match the engine's final cast to its signed 16-bit Score type. */
        out.score = float(int16_t(arithmetic_shift(raw, 5)));
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
    float raw_float = net.bias[f.output_bucket];
    for (int j = 0; j < HIDDEN; ++j) {
        int as = int(out.act[stm][j]);
        int an = int(out.act[nstm][j]);
        raw_float += float(as * as) * net.w2[output_base + j] / 512.0f;
        raw_float += float(an * an) * net.w2[output_base + HIDDEN + j] / 512.0f;
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

static float score_probability(float score) {
    float q = (score - SCORE_OFFSET) / SCORE_SCALE;
    float qm = (-score - SCORE_OFFSET) / SCORE_SCALE;
    return 0.5f * (1.0f + sigmoid(q) - sigmoid(qm));
}

static float score_probability_derivative(float score) {
    float q = (score - SCORE_OFFSET) / SCORE_SCALE;
    float qm = (-score - SCORE_OFFSET) / SCORE_SCALE;
    float sq = sigmoid(q);
    float sqm = sigmoid(qm);
    return 0.5f / SCORE_SCALE *
           (sq * (1.0f - sq) + sqm * (1.0f - sqm));
}

struct ProbabilityTables {
    std::array<float, 65536> probability;
    std::array<float, 65536> derivative;

    ProbabilityTables() {
        for (int score = -32768; score <= 32767; ++score) {
            uint16_t index = uint16_t(int16_t(score));
            probability[index] = score_probability(float(score));
            derivative[index] = score_probability_derivative(float(score));
        }
    }
};

static const ProbabilityTables probability_tables;

struct LossParams {
    float lambda = 1.0f;  // 1 = score target, 0 = game-result target
    float pow_exp = 2.5f;
};

// Accumulates the exact derivative of mean(abs(pt-qf)^pow_exp) into grads.
// Parameters are in engine units: w1/b1 are accumulator units, w2 is the
// i8-like x64 row unit, and bias is raw output units.
static float process_record(const Record& record, const Net& net,
                            const LossParams& params, Gradients& grads, bool qat,
                            const QuantizedNet* qnet) {
    ActiveFeatures f = features(record);
    Forward y = forward(record, net, f, qat, qnet);
    float qf;
    float dprob_dscore;
    if (qat) {
        uint16_t qi = uint16_t(int16_t(int(y.score)));
        qf = probability_tables.probability[qi];
        dprob_dscore = probability_tables.derivative[qi];
    } else {
        qf = score_probability(y.score);
        dprob_dscore = score_probability_derivative(y.score);
    }
    float pf = probability_tables.probability[uint16_t(record.score)];
    float result = 0.5f * float(record.result);
    float target = params.lambda * pf + (1.0f - params.lambda) * result;
    float error = qf - target;
    float abs_error = std::fabs(error);
    float loss;
    if (abs_error == 0.0f) return 0.0f;

    float dloss_dq;
    if (params.pow_exp == 2.5f) {
        float ae_15 = abs_error * std::sqrt(abs_error);
        loss = abs_error * ae_15;
        dloss_dq = params.pow_exp * ae_15;
    } else {
        loss = std::pow(abs_error, params.pow_exp);
        dloss_dq = params.pow_exp * std::pow(abs_error, params.pow_exp - 1.0f);
    }
    if (error < 0.0f) dloss_dq = -dloss_dq;
    float draw = dloss_dq * dprob_dscore / OUTPUT_SHIFT;
    grads.bias[f.output_bucket] += draw;

    int stm = record.stm ? 1 : 0;
    int output_base = f.output_bucket * OUTPUT_STRIDE;
    const float* w2_float = net.w2.data();
    const int8_t* w2_q = qat && qnet ? qnet->w2.data() : nullptr;
    // Division by a power of two equals multiplication by its exact inverse,
    // so these hoists do not change the results beyond association order.
    const float inv512 = 1.0f / 512.0f;
    const float inv256 = 1.0f / 256.0f;
    float dacc[PERSPECTIVES][HIDDEN];
    for (int p = 0; p < PERSPECTIVES; ++p) {
        int w2_base = output_base + ((p == stm) ? 0 : HIDDEN);
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

class WorkerPool {
public:
    ~WorkerPool() { stop(); }

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
            ++generation_;
        }
        wake_.notify_all();
        std::unique_lock<std::mutex> lock(mutex_);
        complete_.wait(lock, [&] { return finished_ == int(workers_.size()); });
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
            grads.zero();
            float loss = 0.0f;
            size_t begin = count * size_t(id) / workers_.size();
            size_t end = count * size_t(id + 1) / workers_.size();
            for (size_t i = begin; i < end; ++i)
                loss += process_record(records[i], *net, *params, grads, qat, qnet);
            local_loss_[id] = loss;
            {
                std::lock_guard<std::mutex> lock(mutex_);
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
    const Record* records_ = nullptr;
    size_t count_ = 0;
    const Net* net_ = nullptr;
    const QuantizedNet* qnet_ = nullptr;
    const LossParams* params_ = nullptr;
    bool qat_ = false;
};

static WorkerPool worker_pool;

void set_threads(int count) {
    worker_pool.set_threads(count);
}

static float process_batch_impl(const Record* records, size_t count, const Net& net,
                                const LossParams& params, Gradients& grads, bool qat,
                                QuantizedNet* qnet_storage = nullptr) {
    if (!count) {
        grads.zero();
        return 0.0f;
    }
    const QuantizedNet* qnet_ptr = nullptr;
    if (qat) {
        if (!qnet_storage) {
            static thread_local QuantizedNet local_qnet;
            qnet_storage = &local_qnet;
        }
        QuantizedNet* qnet = qnet_storage;
        quantize_net(net, *qnet);
        qnet_ptr = qnet;
    }
    if (worker_pool.thread_count() == 0) worker_pool.set_threads(1);
    int thread_count = worker_pool.thread_count();
    if (thread_count <= 1) {
        grads.zero();
        float loss = 0.0f;
        for (size_t i = 0; i < count; ++i)
            loss += process_record(records[i], net, params, grads, qat, qnet_ptr);
        float inv_count = 1.0f / float(count);
        for (float& x : grads.w1) x *= inv_count;
        for (float& x : grads.b1) x *= inv_count;
        for (float& x : grads.w2) x *= inv_count;
        for (float& x : grads.bias) x *= inv_count;
        return loss * inv_count;
    }
    worker_pool.run(records, count, &net, qnet_ptr, &params, qat);
    const std::vector<Gradients>& local_grads = worker_pool.gradients();
    const std::vector<float>& local_loss = worker_pool.losses();
    float loss = 0.0f;
    float inv_count = 1.0f / float(count);
    for (int t = 0; t < thread_count; ++t) loss += local_loss[t];
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
    for (size_t j = 0; j < grads.bias.size(); ++j) {
        float sum = 0.0f;
        for (int t = 0; t < thread_count; ++t) sum += local_grads[t].bias[j];
        grads.bias[j] = sum * inv_count;
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
// The wrapper copies the small parameter vector into the checked Net
// representation, while the large record tensor remains zero-copy.
float process_batch_flat(const uint8_t* records, size_t record_count,
                         const int64_t* indices, size_t batch_count,
                         const float* parameters, float* gradient,
                         const LossParams& params) {
    struct FlatBatchScratch {
        Net net;
        QuantizedNet qnet;
        Gradients grads;
        std::vector<Record> selected;
    };
    static FlatBatchScratch scratch;
    Net& net = scratch.net;
    std::memcpy(net.w1.data(), parameters, sizeof(float) * FEATURES * HIDDEN);
    std::memcpy(net.b1.data(), parameters + FEATURES * HIDDEN, sizeof(float) * HIDDEN);
    std::memcpy(net.w2.data(), parameters + FEATURES * HIDDEN + HIDDEN,
                sizeof(float) * OUTPUT_BUCKETS * OUTPUT_STRIDE);
    std::memcpy(net.bias.data(),
                parameters + FEATURES * HIDDEN + HIDDEN + OUTPUT_BUCKETS * OUTPUT_STRIDE,
                sizeof(float) * OUTPUT_BUCKETS);

    const Record* batch_records;
    if (indices) {
        scratch.selected.resize(batch_count);
        for (size_t i = 0; i < batch_count; ++i) {
            if (indices[i] < 0 || uint64_t(indices[i]) >= record_count)
                throw std::runtime_error("batch index outside record tensor");
            std::memcpy(&scratch.selected[i],
                        records + size_t(indices[i]) * RECORD_SIZE, RECORD_SIZE);
        }
        batch_records = scratch.selected.data();
    } else {
        /* SH01 data starts 16-byte aligned and Record is a trivial fixed-layout
           type, so contiguous ranges can be consumed in place. */
        batch_records = reinterpret_cast<const Record*>(records);
    }
    Gradients& grads = scratch.grads;
    float loss = process_batch_impl(batch_records, batch_count, net, params, grads,
                                    true, &scratch.qnet);
    std::memcpy(gradient, grads.w1.data(), sizeof(float) * FEATURES * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN, grads.b1.data(), sizeof(float) * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN + HIDDEN, grads.w2.data(),
                sizeof(float) * OUTPUT_BUCKETS * OUTPUT_STRIDE);
    std::memcpy(gradient + FEATURES * HIDDEN + HIDDEN + OUTPUT_BUCKETS * OUTPUT_STRIDE,
                grads.bias.data(), sizeof(float) * OUTPUT_BUCKETS);
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
                throw std::runtime_error("cannot load v2/v3 net");
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
        for (int j = 0; j < short_trainer::HIDDEN; ++j) net.b1[j] = 32.0f;
        for (int b = 0; b < short_trainer::OUTPUT_BUCKETS; ++b)
            for (int j = 0; j < short_trainer::HIDDEN; ++j) {
                net.w2[b * short_trainer::OUTPUT_STRIDE + j] = 0.5f;
                net.w2[b * short_trainer::OUTPUT_STRIDE + short_trainer::HIDDEN + j] = -0.25f;
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
        short_trainer::ActiveFeatures first_features = short_trainer::features(batch[0]);
        int output_base = first_features.output_bucket * short_trainer::OUTPUT_STRIDE;
        float bias_numeric = numeric(net.bias[first_features.output_bucket]);
        int row = first_features.rows[0][0];
        float w1_numeric = numeric(net.w1[row * short_trainer::HIDDEN]);
        float w2_numeric = numeric(net.w2[output_base]);
        bool ok = std::fabs(bias_numeric - one_grad.bias[first_features.output_bucket]) < 2.0e-4f &&
                  std::fabs(w1_numeric - one_grad.w1[row * short_trainer::HIDDEN]) < 2.0e-4f &&
                  std::fabs(w2_numeric - one_grad.w2[output_base]) < 2.0e-4f;
        std::printf("file=%s records=%u first=%zu tail=%zu loss=%.9g "
                    "bias_grad=%.9g gradcheck=%s\n", argv[arg], file.count(),
                    batch.size(), tail.size(), loss, grads.bias[first_features.output_bucket],
                    ok ? "ok" : "FAIL");
        if (!ok) {
            std::fprintf(stderr, "numeric/analytic gradient mismatch: bias %.9g/%.9g w1 %.9g/%.9g w2 %.9g/%.9g\n",
                         bias_numeric, one_grad.bias[first_features.output_bucket], w1_numeric,
                         one_grad.w1[row * short_trainer::HIDDEN], w2_numeric,
                         one_grad.w2[output_base]);
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

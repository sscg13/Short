// nnue_trainer.cpp - record loader, v2 forward pass, and scalar gradients.
//
// This is deliberately dependency-free.  The future pybind/torch wrapper can
// hand its parameter and gradient buffers to process_batch() without copying
// the record data or duplicating the feature/derivative logic.

#include <algorithm>
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
constexpr int RECORD_SIZE = 40;
constexpr int HEADER_SIZE = 16;
constexpr int PARAM_COUNT = FEATURES * HIDDEN + HIDDEN + 2 * HIDDEN + 1;

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
    std::vector<float> w2;   // [2][HIDDEN], stm row then nstm row
    float bias = 0.0f;

    Net() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}
};

struct QuantizedNet {
    std::vector<int8_t> w1;
    std::vector<int8_t> b1;
    std::vector<int8_t> w2;
    int16_t bias = 0;

    QuantizedNet() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}
};

static bool load_net(const std::string& path, Net& net) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    uint8_t header[12];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != 12 || std::memcmp(header, "NNUE", 4) != 0 ||
        header[4] != 2 || header[5] != 0 || header[6] != 192 || header[7] != 2 ||
        header[8] != HIDDEN || header[9] != 0 || header[10] != 0 || header[11] != 0)
        return false;
    std::vector<uint8_t> bytes(FEATURES * HIDDEN + HIDDEN + 2 * HIDDEN);
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (file.gcount() != std::streamsize(bytes.size())) return false;
    auto signed_byte = [](uint8_t x) { return float(int8_t(x)); };
    size_t at = 0;
    for (float& x : net.w1) x = signed_byte(bytes[at++]);
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
    int q = int(std::lround(x));
    return std::max(-128, std::min(127, q));
}

static int quant_i16(float x) {
    int q = int(std::lround(x));
    return std::max(-32768, std::min(32767, q));
}

static int arithmetic_shift(int value, int shift) {
    if (value >= 0) return value >> shift;
    return -((-value + ((1 << shift) - 1)) >> shift);
}

static QuantizedNet quantize_net(const Net& net) {
    QuantizedNet q;
    for (size_t i = 0; i < q.w1.size(); ++i) q.w1[i] = int8_t(quant_i8(net.w1[i]));
    for (size_t i = 0; i < q.b1.size(); ++i) q.b1[i] = int8_t(quant_i8(net.b1[i]));
    for (size_t i = 0; i < q.w2.size(); ++i) q.w2[i] = int8_t(quant_i8(net.w2[i]));
    q.bias = int16_t(quant_i16(net.bias));
    return q;
}

struct Gradients {
    std::vector<float> w1;
    std::vector<float> b1;
    std::vector<float> w2;
    float bias = 0.0f;

    Gradients() : w1(FEATURES * HIDDEN), b1(HIDDEN), w2(2 * HIDDEN) {}

    void zero() {
        std::fill(w1.begin(), w1.end(), 0.0f);
        std::fill(b1.begin(), b1.end(), 0.0f);
        std::fill(w2.begin(), w2.end(), 0.0f);
        bias = 0.0f;
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

static void add_perspective(const Record& r, int perspective, int mirror, ActiveFeatures& out) {
    int n = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int record_piece = square(r, sq);
        if (!record_piece) continue;
        bool black = record_piece >= 7;
        int type = black ? record_piece - 6 : record_piece;
        int compact = sq;
        bool enemy = (perspective == 0) ? black : !black;
        int piece = (enemy ? 8 : 0) | type;
        if (perspective) compact = 63 - compact;
        if (mirror) compact = (compact & ~7) | (7 - (compact & 7));
        int row = row_for_piece(piece, compact);
        if (row >= 0) {
            if (n >= 32) throw std::runtime_error("too many active NNUE features");
            out.rows[perspective][n++] = row;
        }
    }
    out.count[perspective] = n;
}

static ActiveFeatures features(const Record& r) {
    ActiveFeatures out;
    int white_file = r.white_king & 7;
    int black_file = r.black_king & 7;
    int mirror[2] = {white_file >= 4, (7 - black_file) >= 4};
    add_perspective(r, 0, mirror[0], out);
    add_perspective(r, 1, mirror[1], out);
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
            for (int j = 0; j < HIDDEN; ++j) {
                int a = qnet->b1[j];
                for (int k = 0; k < f.count[p]; ++k)
                    a += qnet->w1[f.rows[p][k] * HIDDEN + j];
                int act = std::max(0, std::min(255, a));
                out.acc[p][j] = float(a);
                out.act[p][j] = float(act);
            }
        }
        int stm = r.stm ? 1 : 0;
        int nstm = stm ^ 1;
        int raw = qnet->bias;
        for (int j = 0; j < HIDDEN; ++j) {
            int as = int(out.act[stm][j]);
            int an = int(out.act[nstm][j]);
            raw += arithmetic_shift(as * as * qnet->w2[j], 9);
            raw += arithmetic_shift(an * an * qnet->w2[HIDDEN + j], 9);
        }
        out.raw = float(raw);
        out.score = float(arithmetic_shift(raw, 5));
        return out;
    }
    for (int p = 0; p < PERSPECTIVES; ++p) {
        for (int j = 0; j < HIDDEN; ++j) {
            float a = net.b1[j];
            for (int k = 0; k < f.count[p]; ++k)
                a += net.w1[f.rows[p][k] * HIDDEN + j];
            out.acc[p][j] = a;
            out.act[p][j] = std::max(0.0f, std::min(255.0f, a));
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

static float score_probability(float score) {
    float q = (score - SCORE_OFFSET) / SCORE_SCALE;
    float qm = (-score - SCORE_OFFSET) / SCORE_SCALE;
    return 0.5f * (1.0f + sigmoid(q) - sigmoid(qm));
}

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
    float qf = score_probability(y.score);
    float pf = score_probability(float(record.score));
    float result = 0.5f * float(record.result);
    float target = params.lambda * pf + (1.0f - params.lambda) * result;
    float error = qf - target;
    float abs_error = std::fabs(error);
    float loss = std::pow(abs_error, params.pow_exp);
    if (abs_error == 0.0f) return loss;

    float dloss_dq = params.pow_exp * std::pow(abs_error, params.pow_exp - 1.0f);
    if (error < 0.0f) dloss_dq = -dloss_dq;
    float q = (y.score - SCORE_OFFSET) / SCORE_SCALE;
    float qm = (-y.score - SCORE_OFFSET) / SCORE_SCALE;
    float dprob_dscore = 0.5f / SCORE_SCALE *
        (sigmoid(q) * (1.0f - sigmoid(q)) + sigmoid(qm) * (1.0f - sigmoid(qm)));
    float draw = dloss_dq * dprob_dscore / OUTPUT_SHIFT;
    grads.bias += draw;

    int stm = record.stm ? 1 : 0;
    float dacc[PERSPECTIVES][HIDDEN];
    for (int p = 0; p < PERSPECTIVES; ++p) {
        int w2_base = (p == stm) ? 0 : HIDDEN;
        for (int j = 0; j < HIDDEN; ++j) {
            float a = y.act[p][j];
            float w = qat ? float(qnet->w2[w2_base + j])
                          : net.w2[w2_base + j];
            grads.w2[w2_base + j] += draw * a * a / 512.0f;
            dacc[p][j] = (y.acc[p][j] <= 0.0f || y.acc[p][j] >= 255.0f)
                ? 0.0f : draw * (2.0f * a * w / 512.0f);
            grads.b1[j] += dacc[p][j];
        }
        // Each feature row is contiguous in the feature-major w1 layout.
        // Walking rows outside the hidden loop avoids 64 strided writes per
        // active feature and lets the compiler vectorize this short copy-add.
        for (int k = 0; k < f.count[p]; ++k) {
            float* row = &grads.w1[f.rows[p][k] * HIDDEN];
            for (int j = 0; j < HIDDEN; ++j) row[j] += dacc[p][j];
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
        for (int i = 0; i < count; ++i)
            workers_.emplace_back(&WorkerPool::worker, this, i);
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

    void worker(int id) {
        uint64_t seen = 0;
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
                                const LossParams& params, Gradients& grads, bool qat) {
    grads.zero();
    if (!count) return 0.0f;
    QuantizedNet qnet;
    const QuantizedNet* qnet_ptr = nullptr;
    if (qat) {
        qnet = quantize_net(net);
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
        return loss * inv_count;
    }
    worker_pool.run(records, count, &net, qnet_ptr, &params, qat);
    const std::vector<Gradients>& local_grads = worker_pool.gradients();
    const std::vector<float>& local_loss = worker_pool.losses();
    float loss = 0.0f;
    for (int t = 0; t < thread_count; ++t) {
        loss += local_loss[t];
        for (size_t j = 0; j < grads.w1.size(); ++j) grads.w1[j] += local_grads[t].w1[j];
        for (size_t j = 0; j < grads.b1.size(); ++j) grads.b1[j] += local_grads[t].b1[j];
        for (size_t j = 0; j < grads.w2.size(); ++j) grads.w2[j] += local_grads[t].w2[j];
        grads.bias += local_grads[t].bias;
    }
    float inv_count = 1.0f / float(count);
    for (float& x : grads.w1) x *= inv_count;
    for (float& x : grads.b1) x *= inv_count;
    for (float& x : grads.w2) x *= inv_count;
    grads.bias *= inv_count;
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
    float loss = process_batch(selected.data(), selected.size(), net, params, grads);
    std::memcpy(gradient, grads.w1.data(), sizeof(float) * FEATURES * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN, grads.b1.data(), sizeof(float) * HIDDEN);
    std::memcpy(gradient + FEATURES * HIDDEN + HIDDEN, grads.w2.data(),
                sizeof(float) * 2 * HIDDEN);
    gradient[PARAM_COUNT - 1] = grads.bias;
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
                throw std::runtime_error("cannot load v2 net");
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

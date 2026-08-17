// PyTorch boundary for the dependency-free trainer core.
// Build through setup.py; the core is included once so the extension and the
// standalone gradient-check executable share exactly the same implementation.

#include <torch/extension.h>

#include "nnue_trainer.cpp"

static double process_batch_torch(torch::Tensor records, torch::Tensor indices,
                                  torch::Tensor weights, torch::Tensor gradients,
                                  double lambda) {
    TORCH_CHECK(records.device().is_cpu(), "records must be a CPU tensor");
    TORCH_CHECK(records.scalar_type() == torch::kUInt8, "records must be uint8");
    TORCH_CHECK(records.dim() == 2 && records.size(1) == short_trainer::RECORD_SIZE,
                "records must have shape [N, 40]");
    TORCH_CHECK(records.is_contiguous(), "records must be contiguous");

    TORCH_CHECK(indices.device().is_cpu(), "indices must be a CPU tensor");
    TORCH_CHECK(indices.scalar_type() == torch::kInt64, "indices must be int64");
    TORCH_CHECK(indices.dim() == 1, "indices must be one-dimensional");
    TORCH_CHECK(indices.is_contiguous(), "indices must be contiguous");

    TORCH_CHECK(weights.device().is_cpu() && gradients.device().is_cpu(),
                "weights and gradients must be CPU tensors");
    TORCH_CHECK(weights.scalar_type() == torch::kFloat32 &&
                    gradients.scalar_type() == torch::kFloat32,
                "weights and gradients must be float32");
    TORCH_CHECK(weights.dim() == 1 && gradients.dim() == 1 &&
                    weights.numel() == short_trainer::PARAM_COUNT &&
                    gradients.numel() == short_trainer::PARAM_COUNT,
                "unexpected flat parameter/gradient size");
    TORCH_CHECK(weights.is_contiguous() && gradients.is_contiguous(),
                "weights and gradients must be contiguous");

    short_trainer::LossParams params;
    params.lambda = float(lambda);
    pybind11::gil_scoped_release release;
    return short_trainer::process_batch_flat(
        records.data_ptr<uint8_t>(), records.size(0), indices.data_ptr<int64_t>(),
        indices.numel(), weights.data_ptr<float>(), gradients.data_ptr<float>(), params);
}

static double process_range_torch(torch::Tensor records, int64_t first, int64_t count,
                                  torch::Tensor weights, torch::Tensor gradients,
                                  double lambda) {
    TORCH_CHECK(records.device().is_cpu() && records.scalar_type() == torch::kUInt8,
                "records must be a CPU uint8 tensor");
    TORCH_CHECK(records.dim() == 2 && records.size(1) == short_trainer::RECORD_SIZE &&
                    records.is_contiguous(),
                "records must be contiguous with shape [N, 40]");
    TORCH_CHECK(first >= 0 && count >= 0 && first + count <= records.size(0),
                "record range outside tensor");
    TORCH_CHECK(weights.device().is_cpu() && gradients.device().is_cpu() &&
                    weights.scalar_type() == torch::kFloat32 &&
                    gradients.scalar_type() == torch::kFloat32 &&
                    weights.dim() == 1 && gradients.dim() == 1 &&
                    weights.numel() == short_trainer::PARAM_COUNT &&
                    gradients.numel() == short_trainer::PARAM_COUNT &&
                    weights.is_contiguous() && gradients.is_contiguous(),
                "invalid flat parameter or gradient tensor");

    short_trainer::LossParams params;
    params.lambda = float(lambda);
    pybind11::gil_scoped_release release;
    return short_trainer::process_batch_flat(
        records.data_ptr<uint8_t>() + first * short_trainer::RECORD_SIZE,
        size_t(count), nullptr, size_t(count), weights.data_ptr<float>(),
        gradients.data_ptr<float>(), params);
}

static void set_threads(int threads) {
    TORCH_CHECK(threads > 0, "threads must be positive");
    short_trainer::set_threads(threads);
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.def("process_batch", &process_batch_torch,
          "SCReLU^2 forward, loss, and manual gradient accumulation");
    m.def("process_range", &process_range_torch,
          "process a contiguous record range without index indirection");
    m.def("set_threads", &set_threads, "set C++ gradient worker threads");
}

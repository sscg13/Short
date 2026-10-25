// Torch boundary for the shared dependency-free QAT/factored-FD core.
#include <torch/extension.h>
#include "nnue_trainer.cpp"

namespace py = pybind11;
using short_trainer::LossParams;
using short_trainer::TrainingMethod;

static void check_records(const torch::Tensor& records) {
    TORCH_CHECK(records.device().is_cpu() && records.scalar_type() == torch::kUInt8 &&
                    records.dim() == 2 && records.size(1) == short_trainer::RECORD_SIZE &&
                    records.is_contiguous(),
                "records must be contiguous CPU uint8 with shape [N, 40]");
}

static LossParams options(const std::string& method, int curves, bool training,
                          bool freeze_curve, int accumulator_step, int response_step,
                          double lambda, double exponent) {
    TORCH_CHECK(method == "qat" || method == "factored-fd", "unknown training method");
    TORCH_CHECK(curves == 0 || curves == 1 || curves == short_trainer::HIDDEN,
                "curve count must be 0, 1 or 64");
    TORCH_CHECK(method != "qat" || curves == 0, "custom activations require factored-fd");
    TORCH_CHECK(accumulator_step > 0 && accumulator_step <= 255 &&
                    response_step > 0 && response_step <= 65025,
                "invalid finite-difference step");
    TORCH_CHECK(std::isfinite(lambda) && lambda >= 0 && lambda <= 1 &&
                    std::isfinite(exponent) && exponent >= 1, "invalid loss parameters");
    LossParams result;
    result.method = method == "qat" ? TrainingMethod::Qat : TrainingMethod::FactoredFD;
    result.curve_count = curves;
    result.training = training;
    result.freeze_curve = freeze_curve;
    result.accumulator_step = accumulator_step;
    result.response_step = response_step;
    result.lambda = float(lambda);
    result.pow_exp = float(exponent);
    return result;
}

static void check_parameters(const torch::Tensor& weights, const torch::Tensor& gradients,
                             int curves) {
    const int size = short_trainer::PARAM_COUNT + curves * 256;
    for (const auto& tensor : {weights, gradients})
        TORCH_CHECK(tensor.device().is_cpu() && tensor.scalar_type() == torch::kFloat32 &&
                        tensor.dim() == 1 && tensor.numel() == size && tensor.is_contiguous(),
                    "invalid parameter or gradient tensor (expected ", size, " CPU float32 values)");
    const uintptr_t w = reinterpret_cast<uintptr_t>(weights.data_ptr());
    const uintptr_t g = reinterpret_cast<uintptr_t>(gradients.data_ptr());
    const uintptr_t bytes = size * sizeof(float);
    TORCH_CHECK(w + bytes <= g || g + bytes <= w, "weights and gradients must not overlap");
}

static double process_batch(torch::Tensor records, torch::Tensor indices,
                             torch::Tensor weights, torch::Tensor gradients, double lambda,
                             const std::string& method, int curves, bool training,
                             bool freeze_curve, int accumulator_step, int response_step,
                             double exponent) {
    check_records(records);
    auto params = options(method, curves, training, freeze_curve, accumulator_step,
                          response_step, lambda, exponent);
    check_parameters(weights, gradients, curves);
    TORCH_CHECK(indices.device().is_cpu() && indices.scalar_type() == torch::kInt64 &&
                    indices.dim() == 1 && indices.is_contiguous(),
                "indices must be contiguous CPU int64");
    py::gil_scoped_release release;
    return short_trainer::process_batch_flat(
        records.data_ptr<uint8_t>(), records.size(0), indices.data_ptr<int64_t>(),
        indices.numel(), weights.data_ptr<float>(), gradients.data_ptr<float>(), params);
}

static double process_range(torch::Tensor records, int64_t first, int64_t count,
                             torch::Tensor weights, torch::Tensor gradients, double lambda,
                             const std::string& method, int curves, bool training,
                             bool freeze_curve, int accumulator_step, int response_step,
                             double exponent) {
    check_records(records);
    TORCH_CHECK(first >= 0 && count >= 0 && first <= records.size(0) &&
                    count <= records.size(0) - first, "record range outside tensor");
    auto params = options(method, curves, training, freeze_curve, accumulator_step,
                          response_step, lambda, exponent);
    check_parameters(weights, gradients, curves);
    py::gil_scoped_release release;
    return short_trainer::process_batch_flat(
        records.data_ptr<uint8_t>() + first * short_trainer::RECORD_SIZE, size_t(count),
        nullptr, size_t(count), weights.data_ptr<float>(), gradients.data_ptr<float>(), params);
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.attr("NET_VERSION") = short_trainer::NET_VERSION;
    m.attr("TRAINER_API_VERSION") = 2;
    // Existing QAT callers retain the original argument order and defaults.
    m.def("process_batch", &process_batch, py::arg("records"), py::arg("indices"),
          py::arg("weights"), py::arg("gradients"), py::arg("lambda_value"),
          py::arg("method") = "qat", py::arg("curves") = 0, py::arg("training") = true,
          py::arg("freeze_curve") = false, py::arg("accumulator_step") = 4,
          py::arg("response_step") = 256, py::arg("exponent") = 2.5);
    m.def("process_range", &process_range, py::arg("records"), py::arg("first"),
          py::arg("count"), py::arg("weights"), py::arg("gradients"), py::arg("lambda_value"),
          py::arg("method") = "qat", py::arg("curves") = 0, py::arg("training") = true,
          py::arg("freeze_curve") = false, py::arg("accumulator_step") = 4,
          py::arg("response_step") = 256, py::arg("exponent") = 2.5);
    m.def("set_threads", &short_trainer::set_threads, "set gradient worker count");
    m.def("shutdown", &short_trainer::shutdown, "join workers before unloading the extension");
}

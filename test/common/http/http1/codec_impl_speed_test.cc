#include "source/common/http/http1/codec_impl.h"

#include "benchmark/benchmark.h"
#include <cstddef>

namespace Envoy {
namespace Http {

static void fineGrainedBufferWriteHelperVsBuffer(benchmark::State& state) {
  bool use_fine_grained_buffer_write_helper = state.range(0) == 0;
  size_t data_size = state.range(1);
  size_t data_step = state.range(2);
  std::string data_unit(data_step, 'c');
  size_t reserve_cycle = state.range(3);

  for (auto _ : state) { // NOLINT
    if (use_fine_grained_buffer_write_helper) {
      Buffer::WatermarkBuffer buffer(nullptr, nullptr, nullptr);
      Http1::FineGrainedBufferWriteHelper helper(buffer);
      for (size_t i = 0; i < data_size;) {
        helper.reserveBuffer(data_step * reserve_cycle);
        for (size_t e = 0; e < reserve_cycle; e++) {
          helper.writeToBuffer(data_unit);
          i += data_step;
        }
      }
    } else {
      Buffer::WatermarkBuffer buffer(nullptr, nullptr, nullptr);
      for (size_t i = 0; i < data_size;) {
        for (size_t e = 0; e < reserve_cycle; e++) {
          buffer.add(data_unit);
          i += data_step;
        }
      }
    }
  }
}

BENCHMARK(fineGrainedBufferWriteHelperVsBuffer)
    ->Args({0, 64 * 1024 * 1024, 1, 1})
    ->Args({0, 64 * 1024 * 1024, 8, 1})
    ->Args({0, 64 * 1024 * 1024, 16, 1})
    ->Args({0, 64 * 1024 * 1024, 32, 1})
    ->Args({0, 64 * 1024 * 1024, 64, 1})
    ->Args({0, 64 * 1024 * 1024, 128, 1})
    ->Args({0, 64 * 1024 * 1024, 256, 1})
    ->Args({0, 64 * 1024 * 1024, 1024, 1})
    ->Args({0, 64 * 1024 * 1024, 4096, 1})
    ->Args({0, 64 * 1024 * 1024, 8192, 1})
    ->Args({1, 64 * 1024 * 1024, 1, 1})
    ->Args({1, 64 * 1024 * 1024, 8, 1})
    ->Args({1, 64 * 1024 * 1024, 16, 1})
    ->Args({1, 64 * 1024 * 1024, 32, 1})
    ->Args({1, 64 * 1024 * 1024, 64, 1})
    ->Args({1, 64 * 1024 * 1024, 128, 1})
    ->Args({1, 64 * 1024 * 1024, 256, 1})
    ->Args({1, 64 * 1024 * 1024, 1024, 1})
    ->Args({1, 64 * 1024 * 1024, 4096, 1})
    ->Args({1, 64 * 1024 * 1024, 8192, 1})
    ->Args({0, 64 * 1024 * 1024, 1, 2})
    ->Args({0, 64 * 1024 * 1024, 8, 2})
    ->Args({0, 64 * 1024 * 1024, 16, 2})
    ->Args({0, 64 * 1024 * 1024, 32, 2})
    ->Args({0, 64 * 1024 * 1024, 64, 2})
    ->Args({0, 64 * 1024 * 1024, 128, 2})
    ->Args({0, 64 * 1024 * 1024, 256, 2})
    ->Args({0, 64 * 1024 * 1024, 1024, 2})
    ->Args({0, 64 * 1024 * 1024, 4096, 2})
    ->Args({0, 64 * 1024 * 1024, 8192, 2})
    ->Args({1, 64 * 1024 * 1024, 1, 2})
    ->Args({1, 64 * 1024 * 1024, 8, 2})
    ->Args({1, 64 * 1024 * 1024, 16, 2})
    ->Args({1, 64 * 1024 * 1024, 32, 2})
    ->Args({1, 64 * 1024 * 1024, 64, 2})
    ->Args({1, 64 * 1024 * 1024, 128, 2})
    ->Args({1, 64 * 1024 * 1024, 256, 2})
    ->Args({1, 64 * 1024 * 1024, 1024, 2})
    ->Args({1, 64 * 1024 * 1024, 4096, 2})
    ->Args({1, 64 * 1024 * 1024, 8192, 2})
    ->Args({0, 64 * 1024 * 1024, 1, 3})
    ->Args({0, 64 * 1024 * 1024, 8, 3})
    ->Args({0, 64 * 1024 * 1024, 16, 3})
    ->Args({0, 64 * 1024 * 1024, 32, 3})
    ->Args({0, 64 * 1024 * 1024, 64, 3})
    ->Args({0, 64 * 1024 * 1024, 128, 3})
    ->Args({0, 64 * 1024 * 1024, 256, 3})
    ->Args({0, 64 * 1024 * 1024, 1024, 3})
    ->Args({0, 64 * 1024 * 1024, 4096, 3})
    ->Args({0, 64 * 1024 * 1024, 8192, 3})
    ->Args({1, 64 * 1024 * 1024, 1, 3})
    ->Args({1, 64 * 1024 * 1024, 8, 3})
    ->Args({1, 64 * 1024 * 1024, 16, 3})
    ->Args({1, 64 * 1024 * 1024, 32, 3})
    ->Args({1, 64 * 1024 * 1024, 64, 3})
    ->Args({1, 64 * 1024 * 1024, 128, 3})
    ->Args({1, 64 * 1024 * 1024, 256, 3})
    ->Args({1, 64 * 1024 * 1024, 1024, 3})
    ->Args({1, 64 * 1024 * 1024, 4096, 3})
    ->Args({1, 64 * 1024 * 1024, 8192, 3});

} // namespace Http
} // namespace Envoy

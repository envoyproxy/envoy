#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_streamer.h"
#include "source/common/protobuf/utility.h"

#include "benchmark/benchmark.h"

// NOLINT(namespace-envoy)

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_OutputBufferShortOneByOne(benchmark::State& state) {
  absl::string_view fragment = "a";

  // This test is used to compare the performance of addFragments() of BufferOutput
  // and addFragments() of StringOutput at the scenario where the memory could be
  // pre-allocated (for short output scenarios).
  //
  // We repeatedly add a small fragment to the output buffer for 2000 times and then
  // drain the buffer to avoid the buffer from growing too large.

  if (state.range(0) == 0) {
    Envoy::Buffer::OwnedImpl backend_buffer;
    Envoy::Json::BufferOutput output_buffer(backend_buffer);

    for (auto _ : state) { // NOLINT
      std::string result;
      result.clear();
      benchmark::DoNotOptimize(result.size());

      for (int i = 0; i < 2000; i++) {
        output_buffer.add(fragment);
      }

      result = backend_buffer.toString();
      benchmark::DoNotOptimize(result.size());

      backend_buffer.drain(backend_buffer.length());
    }
  } else {
    // Keep the same pre-allocated buffer with Buffer::OwnedImpl.
    std::string backend_buffer;
    Envoy::Json::StringOutput output_buffer(backend_buffer);

    for (auto _ : state) { // NOLINT
      std::string result;
      result.clear();
      benchmark::DoNotOptimize(result.size());

      for (int i = 0; i < 2000; i++) {
        output_buffer.add(fragment);
      }

      result = backend_buffer;
      benchmark::DoNotOptimize(result.size());

      backend_buffer.clear();
    }
  }
}
BENCHMARK(BM_OutputBufferShortOneByOne)->Arg(0)->Arg(1);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_OutputBufferShort(benchmark::State& state) {
  absl::string_view fragment = "a";

  // This test is used to compare the performance of addFragments() of BufferOutput
  // and addFragments() of StringOutput at the scenario where the memory could be
  // pre-allocated (for short output scenarios).
  //
  // We repeatedly add a small fragment to the output buffer for 600 times and then
  // drain the buffer to avoid the buffer from growing too large.

  if (state.range(0) == 0) {
    Envoy::Buffer::OwnedImpl backend_buffer;
    Envoy::Json::BufferOutput output_buffer(backend_buffer);

    for (auto _ : state) { // NOLINT
      std::string result;
      result.clear();
      benchmark::DoNotOptimize(result.size());

      for (int i = 0; i < 600; i++) {
        output_buffer.add(fragment, fragment, fragment);
      }

      result = backend_buffer.toString();
      benchmark::DoNotOptimize(result.size());

      backend_buffer.drain(backend_buffer.length());
    }
  } else {
    // Keep the same pre-allocated buffer with Buffer::OwnedImpl.
    std::string backend_buffer;
    Envoy::Json::StringOutput output_buffer(backend_buffer);

    for (auto _ : state) { // NOLINT
      std::string result;
      result.clear();
      benchmark::DoNotOptimize(result.size());

      for (int i = 0; i < 600; i++) {
        output_buffer.add(fragment, fragment, fragment);
      }

      result = backend_buffer;
      benchmark::DoNotOptimize(result.size());

      backend_buffer.clear();
    }
  }
}
BENCHMARK(BM_OutputBufferShort)->Arg(0)->Arg(1);

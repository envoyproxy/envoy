// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <random>

#include "source/common/common/assert.h"

#include "benchmark/benchmark.h"

namespace Envoy {

class SimpleDataAccessor {
public:
  class SimpleData {
  public:
    uint64_t inc() { return ++data_; }

  private:
    uint64_t data_{};
  };

  SimpleDataAccessor() : data_(std::make_shared<SimpleData>()) {}

  std::shared_ptr<SimpleData> dataSharedPtrByCopy() const { return data_; }
  std::shared_ptr<SimpleData>& dataSharedPtrByRef() { return data_; }
  SimpleData& dataRefFromSharedPtr() { return *data_; }
  SimpleData& dataRefFromStack() { return stack_data_; }

private:
  std::shared_ptr<SimpleData> data_;
  SimpleData stack_data_;
};

static void bmVerifySharedPtrAccessPerformance(benchmark::State& state) {
  SimpleDataAccessor accessor;

  const size_t method = state.range(0);

  if (method == 0) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      for (size_t i = 0; i < 1000; i++) {
        benchmark::DoNotOptimize(accessor.dataSharedPtrByCopy()->inc());
      }
    }
  } else if (method == 1) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      for (size_t i = 0; i < 1000; i++) {
        benchmark::DoNotOptimize(accessor.dataSharedPtrByRef()->inc());
      }
    }
  } else if (method == 2) {
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      for (size_t i = 0; i < 1000; i++) {
        benchmark::DoNotOptimize(accessor.dataRefFromSharedPtr().inc());
      }
    }
  } else {
    ASSERT(method == 3);
    for (auto _ : state) {
      UNREFERENCED_PARAMETER(_);
      for (size_t i = 0; i < 1000; i++) {
        benchmark::DoNotOptimize(accessor.dataRefFromStack().inc());
      }
    }
  }
}
BENCHMARK(bmVerifySharedPtrAccessPerformance)->Args({0})->Args({1})->Args({2})->Args({3});

} // namespace Envoy

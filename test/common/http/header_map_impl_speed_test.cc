#include "common/http/header_map_impl.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {
namespace Http {

/**
 * Add several dummy headers to a HeaderMap.
 * @param num_headers the number of dummy headers to add.
 */
static void addDummyHeaders(HeaderMap& headers, size_t num_headers) {
  const std::string prefix("dummy-key-");
  for (size_t i = 0; i < num_headers; i++) {
    headers.addCopy(LowerCaseString(prefix + std::to_string(i)), "abcd");
  }
}

/** Measure the construction/destruction speed of HeaderMapImpl.*/
static void HeaderMapImplCreate(benchmark::State& state) {
  for (auto _ : state) {
    HeaderMapImpl headers;
    benchmark::DoNotOptimize(headers.size());
  }
}
BENCHMARK(HeaderMapImplCreate);

/**
 * Measure the speed of setting/overwriting a header value. The numeric Arg passed
 * by the BENCHMARK(...) macro call below indicates how many dummy headers this test
 * will add to the HeaderMapImpl before testing the setReference() method. That helps
 * identify whether the speed of setReference() is dependent on the number of other
 * headers in the HeaderMapImpl.
 */
static void HeaderMapImplSetReference(benchmark::State& state) {
  const LowerCaseString key("example-key");
  const std::string value("01234567890123456789");
  HeaderMapImpl headers;
  addDummyHeaders(headers, state.range(0));
  for (auto _ : state) {
    headers.setReference(key, value);
  }
  benchmark::DoNotOptimize(headers.size());
}
BENCHMARK(HeaderMapImplSetReference)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of retrieving a header value. The numeric Arg passed by the
 * BENCHMARK(...) macro call below indicates how many dummy headers this test
 * will add to the HeaderMapImpl during test setup. The relative performance of
 * this test for different Arg values will help reveal how the speed of the get()
 * method depends (or doesn't depend) on the number of other headers in the
 * HeaderMapImpl.
 */
static void HeaderMapImplGet(benchmark::State& state) {
  const LowerCaseString key("example-key");
  const std::string value("01234567890123456789");
  HeaderMapImpl headers;
  addDummyHeaders(headers, state.range(0));
  headers.setReference(key, value);
  size_t successes = 0;
  for (auto _ : state) {
    successes += (headers.get(key) != nullptr);
  }
  benchmark::DoNotOptimize(successes);
}
BENCHMARK(HeaderMapImplGet)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the retrieval speed of a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void HeaderMapImplGetInline(benchmark::State& state) {
  const std::string value("01234567890123456789");
  HeaderMapImpl headers;
  addDummyHeaders(headers, state.range(0));
  headers.insertConnection().value().setReference(value);
  size_t size = 0;
  for (auto _ : state) {
    size += headers.Connection()->value().size();
  }
  benchmark::DoNotOptimize(size);
}
BENCHMARK(HeaderMapImplGetInline)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of writing to a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void HeaderMapImplSetInline(benchmark::State& state) {
  const std::string value("01234567890123456789");
  HeaderMapImpl headers;
  addDummyHeaders(headers, state.range(0));
  for (auto _ : state) {
    headers.insertConnection().value().setReference(value);
  }
  benchmark::DoNotOptimize(headers.size());
}
BENCHMARK(HeaderMapImplSetInline)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

} // namespace Http
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}

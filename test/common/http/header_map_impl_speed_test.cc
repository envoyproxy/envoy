#include "common/http/header_map_impl.h"

#include "benchmark/benchmark.h"

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

/** Measure the construction/destruction speed of RequestHeaderMapImpl.*/
static void headerMapImplCreate(benchmark::State& state) {
  // Make sure first time construction is not counted.
  Http::ResponseHeaderMapImpl::create();
  for (auto _ : state) { // NOLINT
    auto headers = Http::ResponseHeaderMapImpl::create();
    benchmark::DoNotOptimize(headers->size());
  }
}
BENCHMARK(headerMapImplCreate);

/**
 * Measure the speed of setting/overwriting a header value. The numeric Arg passed
 * by the BENCHMARK(...) macro call below indicates how many dummy headers this test
 * will add to the HeaderMapImpl before testing the setReference() method. That helps
 * identify whether the speed of setReference() is dependent on the number of other
 * headers in the HeaderMapImpl.
 */
static void headerMapImplSetReference(benchmark::State& state) {
  const LowerCaseString key("example-key");
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->setReference(key, value);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplSetReference)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of retrieving a header value. The numeric Arg passed by the
 * BENCHMARK(...) macro call below indicates how many dummy headers this test
 * will add to the HeaderMapImpl during test setup. The relative performance of
 * this test for different Arg values will help reveal how the speed of the get()
 * method depends (or doesn't depend) on the number of other headers in the
 * HeaderMapImpl.
 */
static void headerMapImplGet(benchmark::State& state) {
  const LowerCaseString key("example-key");
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  headers->setReference(key, value);
  size_t successes = 0;
  for (auto _ : state) { // NOLINT
    successes += (headers->get(key) != nullptr);
  }
  benchmark::DoNotOptimize(successes);
}
BENCHMARK(headerMapImplGet)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the retrieval speed of a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void headerMapImplGetInline(benchmark::State& state) {
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  headers->setReferenceConnection(value);
  size_t size = 0;
  for (auto _ : state) { // NOLINT
    size += headers->Connection()->value().size();
  }
  benchmark::DoNotOptimize(size);
}
BENCHMARK(headerMapImplGetInline)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of writing to a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void headerMapImplSetInlineMacro(benchmark::State& state) {
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->setReferenceConnection(value);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplSetInlineMacro)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of writing to a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void headerMapImplSetInlineInteger(benchmark::State& state) {
  uint64_t value = 12345;
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->setConnection(value);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplSetInlineInteger)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/** Measure the speed of the byteSize() estimation method. */
static void headerMapImplGetByteSize(benchmark::State& state) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  uint64_t size = 0;
  for (auto _ : state) { // NOLINT
    size += headers->byteSize();
  }
  benchmark::DoNotOptimize(size);
}
BENCHMARK(headerMapImplGetByteSize)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/** Measure the speed of iteration with a lightweight callback. */
static void headerMapImplIterate(benchmark::State& state) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  size_t num_callbacks = 0;
  addDummyHeaders(*headers, state.range(0));
  auto counting_callback = [&num_callbacks](const HeaderEntry&) -> HeaderMap::Iterate {
    num_callbacks++;
    return HeaderMap::Iterate::Continue;
  };
  for (auto _ : state) { // NOLINT
    headers->iterate(counting_callback);
  }
  benchmark::DoNotOptimize(num_callbacks);
}
BENCHMARK(headerMapImplIterate)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of removing a header by key name.
 * @note The measured time for each iteration includes the time needed to add
 *       one copy of the header.
 */
static void headerMapImplRemove(benchmark::State& state) {
  const LowerCaseString key("example-key");
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->addReference(key, value);
    headers->remove(key);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplRemove)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of removing a header by key name, for the special case of
 * a key for which HeaderMapImpl is expected to provide special optimization.
 * @note The measured time for each iteration includes the time needed to add
 *       one copy of the header.
 */
static void headerMapImplRemoveInline(benchmark::State& state) {
  const LowerCaseString key("connection");
  const std::string value("01234567890123456789");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->addReference(key, value);
    headers->remove(key);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplRemoveInline)->Arg(0)->Arg(1)->Arg(10)->Arg(50);

/**
 * Measure the speed of creating a HeaderMapImpl and populating it with a realistic
 * set of response headers.
 */
static void headerMapImplPopulate(benchmark::State& state) {
  const std::pair<LowerCaseString, std::string> headers_to_add[] = {
      {LowerCaseString("cache-control"), "max-age=0, private, must-revalidate"},
      {LowerCaseString("content-encoding"), "gzip"},
      {LowerCaseString("content-type"), "text/html; charset=utf-8"},
      {LowerCaseString("date"), "Wed, 23 Jan 2019 04:00:00 GMT"},
      {LowerCaseString("server"), "envoy"},
      {LowerCaseString("x-custom-header-1"), "example 1"},
      {LowerCaseString("x-custom-header-2"), "example 2"},
      {LowerCaseString("x-custom-header-3"), "example 3"},
      {LowerCaseString("set-cookie"), "_cookie1=12345678; path = /; secure"},
      {LowerCaseString("set-cookie"), "_cookie2=12345678; path = /; secure"},
  };
  for (auto _ : state) { // NOLINT
    auto headers = Http::ResponseHeaderMapImpl::create();
    for (const auto& key_value : headers_to_add) {
      headers->addReference(key_value.first, key_value.second);
    }
    benchmark::DoNotOptimize(headers->size());
  }
}
BENCHMARK(headerMapImplPopulate);

} // namespace Http
} // namespace Envoy

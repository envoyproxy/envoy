#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {

/**
 * Add several dummy headers to a HeaderMap.
 * @param num_headers the number of dummy headers to add.
 */
static void addDummyHeaders(HeaderMap& headers, size_t num_headers,
                            const std::string prefix = "dummy-key-") {
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
BENCHMARK(headerMapImplSetReference)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
    successes += !headers->get(key).empty();
  }
  benchmark::DoNotOptimize(successes);
}
BENCHMARK(headerMapImplGet)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplGetInline)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplSetInlineMacro)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

/**
 * Measure the speed of writing to a header for which HeaderMapImpl is expected to
 * provide special optimizations.
 */
static void headerMapImplSetInlineInteger(benchmark::State& state) {
  uint64_t value = 12345;
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  for (auto _ : state) { // NOLINT
    headers->setContentLength(value++);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplSetInlineInteger)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplGetByteSize)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplIterate)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplRemove)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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
BENCHMARK(headerMapImplRemoveInline)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

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

/**
 * Measure the speed of encoding headers as part of upgraded requests (HTTP/1 to HTTP/2)
 * @note The measured time for each iteration includes the time needed to add
 *       a varying number of headers (set by the benchmark's argument).
 */
static void headerMapImplEmulateH1toH2Upgrade(benchmark::State& state) {
  uint32_t total_len = 0; // Accumulates the length of all header keys and values.
  auto headers = Http::RequestHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  headers->setConnection(Http::Headers::get().ConnectionValues.Upgrade);
  headers->setUpgrade(Http::Headers::get().UpgradeValues.H2c);

  for (auto _ : state) { // NOLINT
    // Emulate the encodeHeaders method upgrade part.
    Http::RequestHeaderMapPtr modified_headers = createHeaderMap<RequestHeaderMapImpl>(*headers);
    benchmark::DoNotOptimize(headers->getUpgradeValue());
    // Emulate the Http::Utility::transformUpgradeRequestFromH1toH2 function.
    modified_headers->setReferenceMethod(Http::Headers::get().MethodValues.Connect);
    modified_headers->setProtocol(headers->getUpgradeValue());
    modified_headers->removeUpgrade();
    modified_headers->removeConnection();
    if (modified_headers->getContentLengthValue() == "0") {
      modified_headers->removeContentLength();
    }
    // Emulate the headers iteration in the buildHeaders method.
    modified_headers->iterate([&total_len](const HeaderEntry& header) -> HeaderMap::Iterate {
      const absl::string_view header_key = header.key().getStringView();
      const absl::string_view header_value = header.value().getStringView();
      total_len += header_key.length() + header_value.length();
      return HeaderMap::Iterate::Continue;
    });
    // modified_headers destruction time also being measured.
  }
  benchmark::DoNotOptimize(headers->size());
  benchmark::DoNotOptimize(total_len);
}
BENCHMARK(headerMapImplEmulateH1toH2Upgrade)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

/**
 * Measure the speed of decoding headers as part of upgraded responses (HTTP/2 to HTTP/1)
 * @note The measured time for each iteration includes the time needed to add
 *       a varying number of headers (set by the benchmark's argument).
 */
static void headerMapImplEmulateH2toH1Upgrade(benchmark::State& state) {
  uint32_t total_len = 0; // Accumulates the length of all header keys and values.
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, state.range(0));
  headers->setStatus(200);

  for (auto _ : state) { // NOLINT
    // Emulate the Http::Utility::transformUpgradeResponseFromH2toH1 function.
    benchmark::DoNotOptimize(headers->getStatusValue());
    headers->setUpgrade(Http::Headers::get().UpgradeValues.H2c);
    headers->setReferenceConnection(Http::Headers::get().ConnectionValues.Upgrade);
    headers->setStatus(101);
    // Emulate a decodeHeaders function that iterates over the headers.
    headers->iterate([&total_len](const HeaderEntry& header) -> HeaderMap::Iterate {
      const absl::string_view header_key = header.key().getStringView();
      const absl::string_view header_value = header.value().getStringView();
      total_len += header_key.length() + header_value.length();
      return HeaderMap::Iterate::Continue;
    });
  }
  benchmark::DoNotOptimize(headers->size());
  benchmark::DoNotOptimize(total_len);
}
BENCHMARK(headerMapImplEmulateH2toH1Upgrade)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

/**
 * Measure the speed of removing a varying number of headers by key name prefix from
 * a header-map that contains 80 headers that do not have that prefix.
 */
static void headerMapImplRemovePrefix(benchmark::State& state) {
  const LowerCaseString prefix("X-prefix");
  auto headers = Http::ResponseHeaderMapImpl::create();
  addDummyHeaders(*headers, 80);
  for (auto _ : state) { // NOLINT
    // Add the headers with the prefix
    state.PauseTiming();
    addDummyHeaders(*headers, state.range(0), prefix.get());
    state.ResumeTiming();
    headers->removePrefix(prefix);
  }
  benchmark::DoNotOptimize(headers->size());
}
BENCHMARK(headerMapImplRemovePrefix)->Arg(0)->Arg(1)->Arg(5)->Arg(10)->Arg(50);

class StaticLookupBenchmarker {
public:
  explicit StaticLookupBenchmarker(std::unique_ptr<HeaderMapImpl> impl)
      : header_map_(std::move(impl)) {}
  absl::optional<HeaderMapImpl::StaticLookupResponse> lookup(absl::string_view key) {
    return header_map_->staticLookup(key);
  }

private:
  std::unique_ptr<HeaderMapImpl> header_map_;
};

static void headerMapImplStaticLookups(benchmark::State& state,
                                       std::unique_ptr<HeaderMapImpl> header_map,
                                       const std::vector<std::string>& keys) {
  int i = keys.size();
  StaticLookupBenchmarker benchmarker{std::move(header_map)};
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto result = benchmarker.lookup(keys[--i]);
    if (i == 0) {
      i = keys.size();
    }
    benchmark::DoNotOptimize(result);
  }
}

static std::vector<std::string> makeMismatchedHeaders() {
  return {
      "x-envoy-banana",
      "some-unknown-header",
      "what-is-this-header",
      "nobody-expects-this-header",
      "another-unexpected-header",
      "x-is-a-letter",
      "x-y-problems-are-the-worst",
  };
}

// Macro is used in the two functions below to populate an array of the common
// set of keys, to use for benchmarking. The INLINE_REQ_HEADERS and
// INLINE_RESP_HEADERS macros do a macro "callback" with just one argument, so
// we can't parameterize `keys` into the macro.
#define ADD_HEADER_TO_KEYS(name) keys.emplace_back(Http::Headers::get().name);
static void bmHeaderMapImplRequestStaticLookupHits(benchmark::State& state) {
  std::vector<std::string> keys;
  INLINE_REQ_HEADERS(ADD_HEADER_TO_KEYS);
  headerMapImplStaticLookups(state, RequestHeaderMapImpl::create(), keys);
}
static void bmHeaderMapImplResponseStaticLookupHits(benchmark::State& state) {
  std::vector<std::string> keys;
  INLINE_RESP_HEADERS(ADD_HEADER_TO_KEYS);
  headerMapImplStaticLookups(state, ResponseHeaderMapImpl::create(), keys);
}
static void bmHeaderMapImplRequestStaticLookupMisses(benchmark::State& state) {
  headerMapImplStaticLookups(state, RequestHeaderMapImpl::create(), makeMismatchedHeaders());
}
static void bmHeaderMapImplResponseStaticLookupMisses(benchmark::State& state) {
  headerMapImplStaticLookups(state, ResponseHeaderMapImpl::create(), makeMismatchedHeaders());
}
BENCHMARK(bmHeaderMapImplRequestStaticLookupHits);
BENCHMARK(bmHeaderMapImplResponseStaticLookupHits);
BENCHMARK(bmHeaderMapImplRequestStaticLookupMisses);
BENCHMARK(bmHeaderMapImplResponseStaticLookupMisses);

} // namespace Http
} // namespace Envoy

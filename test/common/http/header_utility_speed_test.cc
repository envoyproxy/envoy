#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"

#include "test/test_common/test_runtime.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace {

RequestHeaderMapPtr makeHeaders(int64_t values, const std::string& value) {
  auto headers = RequestHeaderMapImpl::create();
  headers->setMethod("GET");
  headers->setPath("/api/items");
  headers->setHost("example.com");
  headers->setScheme("https");
  headers->addCopy(LowerCaseString("accept"), "application/json");
  headers->addCopy(LowerCaseString("x-request-id"), "01234567-89ab-cdef-0123-456789abcdef");
  // A non-inline header preserves repeated entries rather than coalescing on insertion.
  const LowerCaseString key("x-rate-limit-client");
  for (int64_t i = 0; i < values; ++i) {
    headers->addCopy(key, value);
  }
  return headers;
}

// Presence matching must not depend on value length, including empty values.
static void bmHeaderPresence(benchmark::State& state) {
  const auto headers = makeHeaders(state.range(0), std::string(state.range(1), 'a'));
  envoy::config::route::v3::HeaderMatcher config;
  config.set_name("x-rate-limit-client");
  config.set_present_match(state.range(2) != 0);
  config.set_invert_match(state.range(3) != 0);
  config.set_treat_missing_header_as_empty(state.range(4) != 0);
  std::vector<HeaderUtility::HeaderDataPtr> matchers;
  matchers.emplace_back(std::make_unique<HeaderUtility::HeaderDataPresentMatch>(config));

  for (auto _ : state) { // NOLINT
    const bool matches = HeaderUtility::matchHeaders(*headers, matchers);
    benchmark::DoNotOptimize(matches);
  }
}
BENCHMARK(bmHeaderPresence)
    ->ArgsProduct({{0, 1, 4}, {0, 16, 4096}, {0, 1}, {0, 1}, {0, 1}})
    ->ArgNames({"values", "bytes", "present", "invert", "missing_as_empty"});

// Negative control: an exact-value predicate still needs the joined string. Pin
// legacy joined-value semantics so this control is stable across runtime defaults.
static void bmHeaderExactControl(benchmark::State& state) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.reloadable_features.match_headers_individually", "false"}});
  const std::string value(state.range(1), 'a');
  const auto headers = makeHeaders(state.range(0), value);
  std::string expected;
  for (int64_t i = 0; i < state.range(0); ++i) {
    if (i != 0) {
      expected += ',';
    }
    expected += value;
  }
  // Never configure an empty exact matcher (which has special wildcard semantics).
  if (expected.empty()) {
    expected = value;
  }
  if (state.range(2) == 0) {
    expected.back() = 'b';
  }
  envoy::config::route::v3::HeaderMatcher config;
  config.set_name("x-rate-limit-client");
  config.set_exact_match(expected);
  std::vector<HeaderUtility::HeaderDataPtr> matchers;
  matchers.emplace_back(std::make_unique<HeaderUtility::HeaderDataExactMatch>(config));

  for (auto _ : state) { // NOLINT
    const bool matches = HeaderUtility::matchHeaders(*headers, matchers);
    benchmark::DoNotOptimize(matches);
  }
}
BENCHMARK(bmHeaderExactControl)
    ->ArgsProduct({{0, 1, 4}, {16, 4096}, {0, 1}})
    ->ArgNames({"values", "bytes", "matching_value"});

} // namespace
} // namespace Http
} // namespace Envoy

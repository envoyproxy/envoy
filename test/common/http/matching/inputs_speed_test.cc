#include <memory>
#include <string>

#include "source/common/http/header_map_impl.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/inputs.h"
#include "source/common/matcher/exact_map_matcher.h"
#include "source/common/matcher/field_matcher.h"
#include "source/common/matcher/list_matcher.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/stream_info/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace Matching {
namespace {

constexpr absl::string_view HeaderName = "x-tenant";

RequestHeaderMapPtr makeHeaders(int64_t values, const std::string& value) {
  auto headers = RequestHeaderMapImpl::create();
  headers->setMethod("GET");
  headers->setPath("/api/items");
  headers->setHost("example.com");
  headers->setScheme("https");
  headers->addCopy(LowerCaseString("accept"), "application/json");
  for (int64_t i = 0; i < values; ++i) {
    headers->addCopy(LowerCaseString(HeaderName), value);
  }
  return headers;
}

class ExactInputMatcher : public Envoy::Matcher::InputMatcher {
public:
  explicit ExactInputMatcher(std::string value) : value_(std::move(value)) {}

  Envoy::Matcher::MatchResult match(const Envoy::Matcher::DataInputGetResult& input) override {
    const auto data = input.stringData();
    return data && *data == value_ ? Envoy::Matcher::MatchResult::Matched
                                   : Envoy::Matcher::MatchResult::NoMatch;
  }

private:
  const std::string value_;
};

// Extracting a header value for matching.
static void bmHeaderInputGet(benchmark::State& state) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const auto headers = makeHeaders(state.range(0), std::string(state.range(1), 'a'));
  HttpMatchingDataImpl data(stream_info);
  data.onRequestHeaders(*headers);
  HttpRequestHeadersDataInput input{std::string(HeaderName)};

  for (auto _ : state) { // NOLINT
    const auto result = input.get(data);
    benchmark::DoNotOptimize(result.stringData());
  }
}
BENCHMARK(bmHeaderInputGet)->ArgsProduct({{1, 2}, {8, 64, 256}})->ArgNames({"values", "bytes"});

// Exact map keyed on a header. A miss falls back to on_no_match, which reads the input again.
static void bmExactMapOnHeader(benchmark::State& state) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const std::string value(state.range(0), 'a');
  const bool hit = state.range(1) != 0;
  const auto headers = makeHeaders(1, value);
  HttpMatchingDataImpl data(stream_info);
  data.onRequestHeaders(*headers);

  auto matcher = Envoy::Matcher::ExactMapMatcher<HttpMatchingData>::create(
                     std::make_unique<HttpRequestHeadersDataInput>(std::string(HeaderName)),
                     Envoy::Matcher::stringOnMatch<HttpMatchingData>("default"))
                     .value();
  for (int i = 0; i < 100; ++i) {
    matcher->addChild(absl::StrCat("tenant-", i),
                      Envoy::Matcher::stringOnMatch<HttpMatchingData>("tenant"));
  }
  if (hit) {
    matcher->addChild(value, Envoy::Matcher::stringOnMatch<HttpMatchingData>("tenant"));
  }

  for (auto _ : state) { // NOLINT
    const auto result = matcher->match(data);
    benchmark::DoNotOptimize(result.isMatch());
  }
}
BENCHMARK(bmExactMapOnHeader)->ArgsProduct({{8, 64, 256}, {0, 1}})->ArgNames({"bytes", "hit"});

// List of exact predicates on the same header, matching the last one. Each predicate reads the
// input.
static void bmListMatcherOnHeader(benchmark::State& state) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const std::string value(state.range(0), 'a');
  const int64_t predicates = state.range(1);
  const auto headers = makeHeaders(1, value);
  HttpMatchingDataImpl data(stream_info);
  data.onRequestHeaders(*headers);

  Envoy::Matcher::ListMatcher<HttpMatchingData> matcher(
      Envoy::Matcher::stringOnMatch<HttpMatchingData>("default"));
  for (int64_t i = 0; i < predicates; ++i) {
    std::string expected = i + 1 == predicates ? value : absl::StrCat("tenant-", i);
    auto field_matcher = Envoy::Matcher::SingleFieldMatcher<HttpMatchingData>::create(
        std::make_unique<HttpRequestHeadersDataInput>(std::string(HeaderName)),
        std::make_unique<ExactInputMatcher>(std::move(expected)));
    matcher.addMatcher(std::move(field_matcher).value(),
                       Envoy::Matcher::stringOnMatch<HttpMatchingData>("tenant"));
  }

  for (auto _ : state) { // NOLINT
    const auto result = matcher.match(data);
    benchmark::DoNotOptimize(result.isMatch());
  }
}
BENCHMARK(bmListMatcherOnHeader)
    ->ArgsProduct({{8, 64, 256}, {1, 10}})
    ->ArgNames({"bytes", "predicates"});

} // namespace
} // namespace Matching
} // namespace Http
} // namespace Envoy

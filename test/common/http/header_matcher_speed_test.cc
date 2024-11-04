#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_utility.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

#define NEW_HEADER_DATA_MATCHER 0

namespace Envoy {
namespace Http {
namespace {

using envoy::config::route::v3::HeaderMatcher;
using testing::NiceMock;
using testing::ReturnRef;

/**
 * Generates a request with the path:
 * - /shelves/shelf_match/route_match
 */
static Http::TestRequestHeaderMapImpl genMatchingRequestHeaders() {
  return Http::TestRequestHeaderMapImpl{{":authority", "www.google.com"},
                                        {":method", "GET"},
                                        {":path", "/"},
                                        {":match-header", "/shelves/shelf_match/route_match"},
                                        {"x-forwarded-proto", "http"}};
}

/**
 * Generates a request with the path:
 * - /shelves/shelf_x/route_x
 */
static Http::TestRequestHeaderMapImpl genNonMatchingRequestHeaders(int x) {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},
      {":method", "GET"},
      {":path", "/"},
      {":match-header", absl::StrCat("/shelves/shelf_", x, "/route_", x)},
      {"x-forwarded-proto", "http"}};
}

/**
 * Generates the route config for the type of matcher being tested.
 */
#if NEW_HEADER_DATA_MATCHER
static HeaderUtility::HeaderDataPtr genMatchConfig(
#else
static HeaderUtility::HeaderData genMatchConfig(
#endif // NEW_HEADER_DATA_MATCHER
    HeaderMatcher::HeaderMatchSpecifierCase match_type,
    Server::Configuration::ServerFactoryContext& context) {
  // Create the matcher config.
  envoy::config::route::v3::HeaderMatcher header_matcher_config;
  header_matcher_config.set_name("match-header");

  switch (match_type) {
  case HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch: {
    header_matcher_config.set_exact_match(absl::StrCat("/shelves/shelf_match"));
    break;
  }
  case HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch: {
    header_matcher_config.set_exact_match(absl::StrCat("/shelves/shelf_match/route_match"));
    break;
  }
  case HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch: {
    envoy::type::matcher::v3::RegexMatcher* regex =
        header_matcher_config.mutable_safe_regex_match();
    regex->mutable_google_re2();
    regex->set_regex(absl::StrCat("^/shelves/[^\\\\/]+/route_match$"));
    break;
  }
  default:
    PANIC("reached unexpected code");
  }

  // Create the matcher.
#if NEW_HEADER_DATA_MATCHER
  return HeaderUtility::createHeaderData(header_matcher_config, context);
#else
  return HeaderUtility::HeaderData(header_matcher_config, context);
#endif // NEW_HEADER_DATA_MATCHER
}

/**
 * Measure the speed of doing a header match against a route table of varying sizes.
 * Why? Currently, route matching (which invokes header matching) is linear in
 * first-to-win ordering.
 *
 * We construct the first `n - 1` items in the route table so they are not
 * matched by the incoming request. Only the last route will be matched.
 * We then time how long it takes for the request to be matched against the
 * last route.
 */
static void bmRouteTableSize(benchmark::State& state,
                             HeaderMatcher::HeaderMatchSpecifierCase match_type) {
  // Setup router for benchmarking.
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create matcher config.
#if NEW_HEADER_DATA_MATCHER
  HeaderUtility::HeaderDataPtr header_matcher =
#else
  HeaderUtility::HeaderData header_matcher =
#endif // NEW_HEADER_DATA_MATCHER
      genMatchConfig(match_type, factory_context);

  int num_of_matches = state.range(0);
  // Create a vector of `n - 1` non-matching header maps, and the last one will
  // be a matching header map.
  std::vector<Http::TestRequestHeaderMapImpl> input_header_maps;
  input_header_maps.reserve(num_of_matches);
  for (int i = 0; i < num_of_matches - 1; ++i) {
    input_header_maps.emplace_back(genNonMatchingRequestHeaders(i));
  }
  input_header_maps.emplace_back(genMatchingRequestHeaders());

  for (auto _ : state) { // NOLINT
    // Do the actual timing here.
    // The last request will match the matcher's config.
    for (auto it = input_header_maps.begin(); it != input_header_maps.end(); ++it) {
#if NEW_HEADER_DATA_MATCHER
      header_matcher->matchesHeaders(*it);
#else
      HeaderUtility::matchHeaders(*it, header_matcher);
#endif // NEW_HEADER_DATA_MATCHER
    }
  }
}

/**
 * Benchmark a route table with path prefix matchers in the form of:
 * - /shelves/shelf_1/...
 * - /shelves/shelf_2/...
 * - etc.
 */
static void bmRouteTableSizeWithPathPrefixMatch(benchmark::State& state) {
  bmRouteTableSize(state, HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch);
}

/**
 * Benchmark a route table with exact path matchers in the form of:
 * - /shelves/shelf_1/route_1
 * - /shelves/shelf_2/route_2
 * - etc.
 */
static void bmRouteTableSizeWithExactPathMatch(benchmark::State& state) {
  bmRouteTableSize(state, HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch);
}

/**
 * Benchmark a route table with regex path matchers in the form of:
 * - /shelves/{shelf_id}/route_1
 * - /shelves/{shelf_id}/route_2
 * - etc.
 *
 * This represents common OpenAPI path templating.
 */
static void bmRouteTableSizeWithRegexMatch(benchmark::State& state) {
  bmRouteTableSize(state, HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch);
}

BENCHMARK(bmRouteTableSizeWithPathPrefixMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithExactPathMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithRegexMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});

} // namespace
} // namespace Http
} // namespace Envoy

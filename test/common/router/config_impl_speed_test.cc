#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/router/config_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Router {
namespace {

using envoy::config::route::v3::DirectResponseAction;
using envoy::config::route::v3::Route;
using envoy::config::route::v3::RouteConfiguration;
using envoy::config::route::v3::RouteMatch;
using envoy::config::route::v3::VirtualHost;
using testing::NiceMock;
using testing::ReturnRef;

/**
 * Generates a request with the path:
 * - /shelves/shelf_x/route_x
 */
static Http::TestRequestHeaderMapImpl genRequestHeaders(int route_num,
                                      std::string req_path_header) {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},
      {":method", "GET"},
      {":path", absl::StrCat(req_path_header, route_num, "/route_", route_num)},
      {"x-forwarded-proto", "http"}};
}

/**
 * Generates the route config for the type of matcher being tested.
 */
static RouteConfiguration genRouteConfig(benchmark::State& state,
                                         RouteMatch::PathSpecifierCase match_type,
                                         std::string path_rule_base) {
  // Create the base route config.
  RouteConfiguration route_config;
  VirtualHost* v_host = route_config.add_virtual_hosts();
  v_host->set_name("default");
  v_host->add_domains("*");

  // Create `n` regex routes. The last route will be the only one matched.
  for (int i = 0; i < state.range(0); ++i) {
    Route* route = v_host->add_routes();
    DirectResponseAction* direct_response = route->mutable_direct_response();
    direct_response->set_status(200);
    RouteMatch* match = route->mutable_match();

    switch (match_type) {
    case RouteMatch::PathSpecifierCase::kPrefix: {
      match->set_prefix(absl::StrCat(path_rule_base, i, "/"));
      break;
    }
    case RouteMatch::PathSpecifierCase::kPath: {
      match->set_prefix(absl::StrCat(path_rule_base, i, "/route_", i));
      break;
    }
    case RouteMatch::PathSpecifierCase::kSafeRegex: {
      envoy::type::matcher::v3::RegexMatcher* regex = match->mutable_safe_regex();
      regex->mutable_google_re2();
      regex->set_regex(absl::StrCat(path_rule_base, i, "$"));
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  return route_config;
}

/**
 * Measure the speed of doing a route match against a route table of varying sizes.
 * Why? Currently, route matching is linear in first-to-win ordering.
 *
 * We construct the first `n - 1` items in the route table so they are not
 * matched by the incoming request. Only the last route will be matched.
 * We then time how long it takes for the request to be matched against the
 * last route.
 */
static void bmRouteTableSize(benchmark::State& state,
                             RouteMatch::PathSpecifierCase match_type,
                             std::string path_rule_base,
                             std::string req_path_header) {
  // Setup router for benchmarking.
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create router config.
  ConfigImpl config(genRouteConfig(state, match_type, path_rule_base),
                     OptionalHttpFilters(), factory_context,
                    ProtobufMessage::getNullValidationVisitor(), true);

  Http::TestRequestHeaderMapImpl req =
      genRequestHeaders(state.range(0) - 1, req_path_header);

  for (auto _ : state) { // NOLINT
    // Do the actual timing here.
    // Single request that will match the last route in the config.
    config.route(req, stream_info, 0);
  }
}

/**
 * Benchmark a route table with path prefix matchers in the form of:
 * - /shelves/shelf_1/...
 * - /shelves/shelf_2/...
 * - etc.
 */
static void bmRouteTableSizeWithPathPrefixMatchFixPathLen(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPrefix,
                  "/shelves/shelf_", "/shelves/shelf_");
}

/**
 * Benchmark a route table with exact path matchers in the form of:
 * - /shelves/shelf_1/route_1
 * - /shelves/shelf_2/route_2
 * - etc.
 */
static void bmRouteTableSizeWithExactPathMatchFixPathLen(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPath,
                  "/shelves/shelf_", "/shelves/shelf_");
}

/**
 * Benchmark a route table with regex path matchers in the form of:
 * - /shelves/{shelf_id}/route_1
 * - /shelves/{shelf_id}/route_2
 * - etc.
 *
 * This represents common OpenAPI path templating.
 */
static void bmRouteTableSizeWithRegexMatchFixPathLen(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kSafeRegex,
                  "^/shelves/[^\\\\/]+/route_", "/shelves/shelf_");
}

BENCHMARK(bmRouteTableSizeWithPathPrefixMatchFixPathLen)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithExactPathMatchFixPathLen)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithRegexMatchFixPathLen)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});

// Generate path rule prefix based on the pattern 'abcdefghij', and path_len
static std::string GetPrefixPathRuleFromBase(int path_len) {
  std::string path_rule_base = "abcdefghij"; // pattern
  std::string path_rule = "";

  // repeating the pattern path_len/10 times
  for (int repeat = 0; repeat < path_len / 10;  ++repeat) {
    absl::StrAppend(&path_rule, path_rule_base);
  }
  return path_rule;
}

/**
 * Benchmark a route table with prefix path matchers with different path rule length:
 * - /abcdefghij...<repeat n times>_n
 * - etc.
 */
static void bmRouteTableSizeWithPathPrefixMatchVaryPathLen(benchmark::State& state) {
  std::string path_rule = GetPrefixPathRuleFromBase(state.range(1));
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPrefix,
                  path_rule, path_rule);
}

// Arg1 is the path rule table size. Arg2 is the path rule length.
BENCHMARK(bmRouteTableSizeWithPathPrefixMatchVaryPathLen)
    ->ArgPair(1, 10)
    ->ArgPair(10, 10)
    ->ArgPair(50, 10)
    ->ArgPair(100, 10)
    ->ArgPair(500, 10)
    ->ArgPair(1000, 10)
    ->ArgPair(1500, 10)
    ->ArgPair(2000, 10)
    ->ArgPair(5000, 10)
    ->ArgPair(10000, 10)
    ->ArgPair(1, 50)
    ->ArgPair(10, 50)
    ->ArgPair(50, 50)
    ->ArgPair(100, 50)
    ->ArgPair(500, 50)
    ->ArgPair(1000, 50)
    ->ArgPair(1500, 50)
    ->ArgPair(2000, 50)
    ->ArgPair(5000, 50)
    ->ArgPair(10000, 50)
    ->ArgPair(1, 100)
    ->ArgPair(10, 100)
    ->ArgPair(50, 100)
    ->ArgPair(100, 100)
    ->ArgPair(500, 100)
    ->ArgPair(1000, 100)
    ->ArgPair(1500, 100)
    ->ArgPair(2000, 100)
    ->ArgPair(5000, 100)
    ->ArgPair(10000, 100)
    ->ArgPair(1, 500)
    ->ArgPair(10, 500)
    ->ArgPair(50, 500)
    ->ArgPair(100, 500)
    ->ArgPair(500, 500)
    ->ArgPair(1000, 500)
    ->ArgPair(1500, 500)
    ->ArgPair(2000, 500)
    ->ArgPair(5000, 500)
    ->ArgPair(10000, 500)
    ->ArgPair(1, 1000)
    ->ArgPair(10, 1000)
    ->ArgPair(50, 1000)
    ->ArgPair(100, 1000)
    ->ArgPair(500, 1000)
    ->ArgPair(1000, 1000)
    ->ArgPair(1500, 1000)
    ->ArgPair(2000, 1000)
    ->ArgPair(5000, 1000)
    ->ArgPair(10000, 1000)
    ->ArgPair(1, 2000)
    ->ArgPair(10, 2000)
    ->ArgPair(50, 2000)
    ->ArgPair(100, 2000)
    ->ArgPair(500, 2000)
    ->ArgPair(1000, 2000)
    ->ArgPair(1500, 2000)
    ->ArgPair(2000, 2000)
    ->ArgPair(5000, 2000)
    ->ArgPair(10000, 2000);

/**
 * Benchmark a route table with exact path matchers with different path rule length:
 * - /abcdefghij...<repeat n times>_n
 * - etc.
 */
static void bmRouteTableSizeWithExactPathMatchVaryPathLen(benchmark::State& state) {
  std::string path_rule = GetPrefixPathRuleFromBase(state.range(1));
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPath,
                  path_rule, path_rule);
}

// Arg1 is the path rule table size. Arg2 is the path rule length.
BENCHMARK(bmRouteTableSizeWithExactPathMatchVaryPathLen)
    ->ArgPair(1, 10)
    ->ArgPair(10, 10)
    ->ArgPair(50, 10)
    ->ArgPair(100, 10)
    ->ArgPair(500, 10)
    ->ArgPair(1000, 10)
    ->ArgPair(1500, 10)
    ->ArgPair(2000, 10)
    ->ArgPair(5000, 10)
    ->ArgPair(10000, 10)
    ->ArgPair(1, 50)
    ->ArgPair(10, 50)
    ->ArgPair(50, 50)
    ->ArgPair(100, 50)
    ->ArgPair(500, 50)
    ->ArgPair(1000, 50)
    ->ArgPair(1500, 50)
    ->ArgPair(2000, 50)
    ->ArgPair(5000, 50)
    ->ArgPair(10000, 50)
    ->ArgPair(1, 100)
    ->ArgPair(10, 100)
    ->ArgPair(50, 100)
    ->ArgPair(100, 100)
    ->ArgPair(500, 100)
    ->ArgPair(1000, 100)
    ->ArgPair(1500, 100)
    ->ArgPair(2000, 100)
    ->ArgPair(5000, 100)
    ->ArgPair(10000, 100)
    ->ArgPair(1, 500)
    ->ArgPair(10, 500)
    ->ArgPair(50, 500)
    ->ArgPair(100, 500)
    ->ArgPair(500, 500)
    ->ArgPair(1000, 500)
    ->ArgPair(1500, 500)
    ->ArgPair(2000, 500)
    ->ArgPair(5000, 500)
    ->ArgPair(10000, 500)
    ->ArgPair(1, 1000)
    ->ArgPair(10, 1000)
    ->ArgPair(50, 1000)
    ->ArgPair(100, 1000)
    ->ArgPair(500, 1000)
    ->ArgPair(1000, 1000)
    ->ArgPair(1500, 1000)
    ->ArgPair(2000, 1000)
    ->ArgPair(5000, 1000)
    ->ArgPair(10000, 1000)
    ->ArgPair(1, 2000)
    ->ArgPair(10, 2000)
    ->ArgPair(50, 2000)
    ->ArgPair(100, 2000)
    ->ArgPair(500, 2000)
    ->ArgPair(1000, 2000)
    ->ArgPair(1500, 2000)
    ->ArgPair(2000, 2000)
    ->ArgPair(5000, 2000)
    ->ArgPair(10000, 2000);

/**
 * Benchmark a route table with regex path matchers in the form of:
 * - "^/api/_?/v\\d{2}"                                       : ~10 bytes regex complexity
 * - "^/videos/_?/v\\d{3}/ba[r|z].m?[4s|pg|p4]"               " ~50 bytes regex complexity
 * - "^/.* /foo\\d{3}\\W{1,2}V+T?/ba[r|z]/A{2}.[h|w][^a-z]{1}": ~100 bytes regex complexity
 * - etc.
 */

// Generate RegEx path rule based on path rule length.
static std::string GetRegexPathRuleFromBase(int path_len) {
  std::string path_rule = "";

  switch (path_len) {
    case 10:
      absl::StrAppend(&path_rule, "^/api/_?/v\\d{2}");
      break;
    case 50:
      absl::StrAppend(&path_rule, "^/videos/_?/v\\d{3}/ba[r|z].m?[4s|pg|p4]");
      break;
    case 100:
      absl::StrAppend(&path_rule,
            "^/.*/foo\\d{3}\\W{1,2}V+T?/ba[r|z]/A{2}.[h|w][^a-z]{1}");
      break;
  }
  return path_rule;
}

// Generate request header to match corresponding RexEx rule.
static std::string GetRegexPathHeaderFromBase(int path_len) {
  std::string path_header = "";

  switch (path_len) {
    case 10:
      absl::StrAppend(&path_header, "/api/_/v12");
      break;
    case 50:
      absl::StrAppend(&path_header, "/videos/_/v123/bar.mp4");
      break;
    case 100:
      absl::StrAppend(&path_header,
            "/test/foo123&&VT/baz/AA.wB");
      break;
  }

  return path_header;
}

static void bmRouteTableSizeWithRegexMatchVaryPathLen(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kSafeRegex,
                  GetRegexPathRuleFromBase(state.range(1)),
                  GetRegexPathHeaderFromBase(state.range(1)));
}

// Arg1 is the path rule table size. Arg2 is the approximately RegEx bytes.
BENCHMARK(bmRouteTableSizeWithRegexMatchVaryPathLen)
    ->ArgPair(1, 10)
    ->ArgPair(10, 10)
    ->ArgPair(50, 10)
    ->ArgPair(100, 10)
    ->ArgPair(500, 10)
    ->ArgPair(1000, 10)
    ->ArgPair(1500, 10)
    ->ArgPair(2000, 10)
    ->ArgPair(5000, 10)
    ->ArgPair(10000, 10)
    ->ArgPair(1, 50)
    ->ArgPair(10, 50)
    ->ArgPair(50, 50)
    ->ArgPair(100, 50)
    ->ArgPair(500, 50)
    ->ArgPair(1000, 50)
    ->ArgPair(1500, 50)
    ->ArgPair(2000, 50)
    ->ArgPair(5000, 50)
    ->ArgPair(10000, 50)
    ->ArgPair(1, 100)
    ->ArgPair(10, 100)
    ->ArgPair(50, 100)
    ->ArgPair(100, 100)
    ->ArgPair(500, 100)
    ->ArgPair(1000, 100)
    ->ArgPair(1500, 100)
    ->ArgPair(2000, 100)
    ->ArgPair(5000, 100)
    ->ArgPair(10000, 100);

} // namespace
} // namespace Router
} // namespace Envoy

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/router/config_impl.h"

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
static Http::TestRequestHeaderMapImpl genRequestHeaders(int route_num) {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/shelves/shelf_", route_num, "/route_", route_num)},
      {"x-forwarded-proto", "http"}};
}

/**
 * Generates the route config for the type of matcher being tested.
 */
static RouteConfiguration genRouteConfig(benchmark::State& state,
                                         RouteMatch::PathSpecifierCase match_type) {
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
      match->set_prefix(absl::StrCat("/shelves/shelf_", i, "/"));
      break;
    }
    case RouteMatch::PathSpecifierCase::kPath: {
      match->set_prefix(absl::StrCat("/shelves/shelf_", i, "/route_", i));
      break;
    }
    case RouteMatch::PathSpecifierCase::kSafeRegex: {
      envoy::type::matcher::v3::RegexMatcher* regex = match->mutable_safe_regex();
      regex->mutable_google_re2();
      regex->set_regex(absl::StrCat("^/shelves/[^\\\\/]+/route_", i, "$"));
      break;
    }
    default:
      PANIC("reached unexpected code");
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
static void bmRouteTableSize(benchmark::State& state, RouteMatch::PathSpecifierCase match_type) {
  // Setup router for benchmarking.
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create router config.
  std::shared_ptr<ConfigImpl> config =
      *ConfigImpl::create(genRouteConfig(state, match_type), factory_context,
                          ProtobufMessage::getNullValidationVisitor(), true);

  for (auto _ : state) { // NOLINT
    // Do the actual timing here.
    // Single request that will match the last route in the config.
    int last_route_num = state.range(0) - 1;
    config->route(genRequestHeaders(last_route_num), stream_info, 0);
  }
}

/**
 * Benchmark a route table with path prefix matchers in the form of:
 * - /shelves/shelf_1/...
 * - /shelves/shelf_2/...
 * - etc.
 */
static void bmRouteTableSizeWithPathPrefixMatch(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPrefix);
}

/**
 * Benchmark a route table with exact path matchers in the form of:
 * - /shelves/shelf_1/route_1
 * - /shelves/shelf_2/route_2
 * - etc.
 */
static void bmRouteTableSizeWithExactPathMatch(benchmark::State& state) {
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kPath);
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
  bmRouteTableSize(state, RouteMatch::PathSpecifierCase::kSafeRegex);
}

BENCHMARK(bmRouteTableSizeWithPathPrefixMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithExactPathMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithRegexMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});

} // namespace
} // namespace Router
} // namespace Envoy

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
static Http::TestRequestHeaderMapImpl genRequestHeaders(int shelf_id, int route_num) {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/shelves/shelf_", shelf_id, "/route_", route_num)},
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
 * Generates a route config using matcher tree semantics with n entries.
 */
static RouteConfiguration genMatcherTreeRouteConfig(benchmark::State& state) {
  RouteConfiguration route_config;
  VirtualHost* v_host = route_config.add_virtual_hosts();
  v_host->set_name("default");
  v_host->add_domains("*");

  auto* matcher = v_host->mutable_matcher();
  auto* matcher_tree = matcher->mutable_matcher_tree();

  // Configure the input to match on the :path header
  auto* input = matcher_tree->mutable_input();
  input->set_name("request-headers");
  auto* typed_config = input->mutable_typed_config();
  typed_config->set_type_url(
      "type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput");

  // Create the exact match map
  auto* exact_match_map = matcher_tree->mutable_exact_match_map();
  auto* map = exact_match_map->mutable_map();

  // Create n routes in the matcher tree
  for (int i = 0; i < state.range(0); ++i) {
    std::string path = absl::StrCat("/shelves/shelf_", i, "/route_", i);

    // Create the route configuration
    Route route;
    auto* match = route.mutable_match();
    match->set_prefix("/");

    DirectResponseAction* direct_response = route.mutable_direct_response();
    direct_response->set_status(200);

    auto* header = route.add_request_headers_to_add();
    header->mutable_header()->set_key("x-route-header");
    header->mutable_header()->set_value(absl::StrCat("matcher_tree_", i));

    // Create the matcher action
    auto& matcher_action = (*map)[path];
    matcher_action.mutable_action()->set_name("route");
    matcher_action.mutable_action()->mutable_typed_config()->PackFrom(route);
  }

  return route_config;
}

/**
 * Generates a route config using prefix matcher tree semantics with n shelf groups,
 * each containing m routes.
 */
static RouteConfiguration genPrefixMatcherTreeRouteConfig(benchmark::State& state) {
  RouteConfiguration route_config;
  VirtualHost* v_host = route_config.add_virtual_hosts();
  v_host->set_name("default");
  v_host->add_domains("*");

  auto* matcher = v_host->mutable_matcher();
  auto* matcher_tree = matcher->mutable_matcher_tree();

  // Configure the input to match on the :path header
  auto* input = matcher_tree->mutable_input();
  input->set_name("request-headers");
  auto* typed_config = input->mutable_typed_config();
  typed_config->set_type_url(
      "type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput");

  // Create the prefix match map
  auto* prefix_match_map = matcher_tree->mutable_prefix_match_map();
  auto* map = prefix_match_map->mutable_map();

  // We'll create ``sqrt(n)`` shelves with ``sqrt(n)`` routes each to
  // get n total routes
  int shelf_count = static_cast<int>(std::sqrt(state.range(0)));
  int routes_per_shelf = shelf_count;

  // Create shelves with their routes
  for (int shelf = 0; shelf < shelf_count; ++shelf) {
    std::string shelf_prefix = absl::StrCat("/shelves/shelf_", shelf);

    // Create a RouteList for this shelf prefix
    envoy::config::route::v3::RouteList route_list;

    // Add routes for this shelf
    for (int route = 0; route < routes_per_shelf; ++route) {
      auto* new_route = route_list.add_routes();

      // Set up the route match
      auto* match = new_route->mutable_match();
      match->set_prefix(absl::StrCat(shelf_prefix, "/route_", route));

      // Set up the route action
      DirectResponseAction* direct_response = new_route->mutable_direct_response();
      direct_response->set_status(200);

      // Add a header
      auto* header = new_route->add_request_headers_to_add();
      header->mutable_header()->set_key("x-route-header");
      header->mutable_header()->set_value(absl::StrCat("match_tree_", shelf, "_", route));
    }

    // Add a catch-all route for this shelf
    auto* catch_all = route_list.add_routes();
    catch_all->mutable_match()->set_prefix(shelf_prefix);
    catch_all->mutable_direct_response()->set_status(200);
    auto* catch_all_header = catch_all->add_request_headers_to_add();
    catch_all_header->mutable_header()->set_key("x-route-header");
    catch_all_header->mutable_header()->set_value(absl::StrCat("match_tree_", shelf, "_default"));

    // Create the matcher action for this shelf
    auto& matcher_action = (*map)[shelf_prefix];
    matcher_action.mutable_action()->set_name("route");
    matcher_action.mutable_action()->mutable_typed_config()->PackFrom(route_list);
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
    config->route(genRequestHeaders(last_route_num, last_route_num), stream_info, 0);
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

/**
 * Benchmark matcher tree route matching performance with exact path matchers in the form of:
 * - /shelves/shelf_1/route_1
 * - /shelves/shelf_2/route_2
 * - etc.
 */
static void bmRouteTableSizeWithExactMatcherTree(benchmark::State& state) {
  // Setup router for benchmarking
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create router config with matcher tree
  std::shared_ptr<ConfigImpl> config =
      *ConfigImpl::create(genMatcherTreeRouteConfig(state), factory_context,
                          ProtobufMessage::getNullValidationVisitor(), true);

  for (auto _ : state) {
    // Match against the last route in the config
    int last_route_num = state.range(0) - 1;
    config->route(genRequestHeaders(last_route_num, last_route_num), stream_info, 0);
  }
}

/**
 * Benchmark matcher tree route matching performance with prefix path matchers in the form of:
 * - /shelves/shelf_1/...
 * - /shelves/shelf_1/...
 * - ...
 * - /shelves/shelf_2/...
 * - /shelves/shelf_2/...
 * - ...
 * - etc.
 */
static void bmRouteTableSizeWithPrefixMatcherTree(benchmark::State& state) {
  // Setup router for benchmarking
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create router config with matcher tree
  std::shared_ptr<ConfigImpl> config =
      *ConfigImpl::create(genPrefixMatcherTreeRouteConfig(state), factory_context,
                          ProtobufMessage::getNullValidationVisitor(), true);

  for (auto _ : state) {
    // Match against the last route in the last shelf
    int shelf_count = static_cast<int>(std::sqrt(state.range(0)));
    int last_shelf_num = shelf_count - 1;
    int last_route_num = shelf_count - 1;
    config->route(genRequestHeaders(last_shelf_num, last_route_num), stream_info, 0);
  }
}

BENCHMARK(bmRouteTableSizeWithPathPrefixMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithExactPathMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithRegexMatch)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});

BENCHMARK(bmRouteTableSizeWithExactMatcherTree)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});
BENCHMARK(bmRouteTableSizeWithPrefixMatcherTree)->RangeMultiplier(2)->Ranges({{1, 2 << 13}});

} // namespace
} // namespace Router
} // namespace Envoy

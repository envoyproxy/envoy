#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "common/router/config_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Router {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

static Http::TestRequestHeaderMapImpl genRequestHeaders(int route_num) {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/shelves/shelf_id_1/books/book_id_", route_num)},
      {"x-forwarded-proto", "http"}};
}

/**
 * Measure the speed of doing a route match against a route table of varying sizes.
 * Why? Currently, route matching is linear in first-to-win ordering.
 *
 * In this benchmark, we use regex route matches. We construct the first `n - 1`
 * items in the route table so they are not matched by the incoming request.
 * Only the last route will be matched.
 *
 * We then time how long it takes for the request to be matched against the last route.
 */
static void BM_RouteTableSize(benchmark::State& state) {
  // Setup router for benchmarking.
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_query_string_in_path_redirects", "false"}});
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));

  // Create the base route config.
  envoy::config::route::v3::RouteConfiguration route_config;
  envoy::config::route::v3::VirtualHost* v_host = route_config.add_virtual_hosts();
  v_host->set_name("default");
  v_host->add_domains("*");

  // Create `n` regex routes. The last route will be the only one matched.
  for (int i = 0; i < state.range(0); ++i) {
    envoy::config::route::v3::Route* route = v_host->add_routes();
    envoy::config::route::v3::RouteMatch* match = route->mutable_match();
    envoy::type::matcher::v3::RegexMatcher* regex = match->mutable_safe_regex();
    regex->mutable_google_re2();
    regex->set_regex(absl::StrCat("^/shelves/[^\\\\/]+/books/book_id_", i, "$"));
  }

  // Create router config.
  ConfigImpl config(route_config, factory_context, ProtobufMessage::getNullValidationVisitor(),
                    true);

  for (auto _ : state) {
    // Do the actual timing here.
    // Single request that will match the last route in the config.
    config.route(genRequestHeaders(state.range(0) - 1), stream_info, 0);
  }
}

BENCHMARK(BM_RouteTableSize)->RangeMultiplier(2)->Ranges({{1, 2 << 14}});

} // namespace
} // namespace Router
} // namespace Envoy

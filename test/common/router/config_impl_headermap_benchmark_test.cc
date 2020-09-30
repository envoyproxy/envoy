#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "common/http/header_map_impl.h"
#include "common/router/config_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

using testing::ReturnRef;

namespace Envoy {
namespace Router {

/**
 * Measure the time it takes to iterate over country route configurations until
 * the default route is taken. This emulates a case where the router has 250
 * different configuration (for 250 countries), and multiple requests that
 * aren't matched are tested against. The test allows the performance comparison
 * of different header map implementations.
 *
 * Note: the benchmark includes the time to setup the config routes and add all
 * the request headers once.
 * */
static void manyCountryRoutesLongHeaders(benchmark::State& state) {
  // Add a route configuration with multiple route, each has a different
  // x-country<N> header required to that route.
  const size_t countries_num = 250;
  const Http::LowerCaseString country_header_name("x-country");
  envoy::config::route::v3::RouteConfiguration proto_config;
  auto main_virtual_host = proto_config.mutable_virtual_hosts()->Add();
  main_virtual_host->set_name("default");
  main_virtual_host->mutable_domains()->Add("*");
  // Add countries routes.
  std::vector<std::string> countries;
  for (size_t i = 0; i < countries_num; i++) {
    auto country_name = absl::StrCat("country", i);
    countries.push_back(country_name);
    // Add the country route.
    auto new_routes = main_virtual_host->mutable_routes()->Add();
    new_routes->mutable_match()->set_prefix("/");
    new_routes->mutable_route()->set_cluster(country_name);
    auto headers_matcher = new_routes->mutable_match()->mutable_headers()->Add();
    headers_matcher->set_name(country_header_name.get());
    headers_matcher->set_exact_match(country_name);
  }
  // Add the default route.
  auto new_routes = main_virtual_host->mutable_routes()->Add();
  new_routes->mutable_match()->set_prefix("/");
  new_routes->mutable_route()->set_cluster("default");

  // Setup the config parsing.
  Api::ApiPtr api(Api::createApiForTest());
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  ON_CALL(factory_context, api()).WillByDefault(ReturnRef(*api));
  ConfigImpl config(proto_config, factory_context, ProtobufMessage::getNullValidationVisitor(),
                    true);

  const auto stream_info = NiceMock<Envoy::StreamInfo::MockStreamInfo>();
  auto req_headers = Http::TestRequestHeaderMapImpl{{":authority", "www.lyft.com"},
                                                    {":path", "/"},
                                                    {":method", "GET"},
                                                    {"x-forwarded-proto", "http"}};
  // Add dummy headers to reach ~100 headers (limit per request).
  for (int i = 0; i < 90; i++) {
    req_headers.addCopy(Http::LowerCaseString(absl::StrCat("dummyheader", i)), "some_value");
  }
  req_headers.addReferenceKey(country_header_name, absl::StrCat("country", countries_num));
  for (auto _ : state) { // NOLINT
    auto& result = config.route(req_headers, stream_info, 0)->routeEntry()->clusterName();
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(manyCountryRoutesLongHeaders)
    ->Arg(0)
    ->Arg(1)
    ->Arg(5)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(5000)
    ->Arg(10000);

} // namespace Router
} // namespace Envoy

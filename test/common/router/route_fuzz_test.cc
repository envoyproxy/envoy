#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/router/config_impl.h"

#include "test/common/router/route_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/instance.h"

namespace Envoy {
namespace Router {
namespace {

// Remove regex matching route configs.
envoy::config::route::v3::RouteConfiguration
cleanRouteConfig(envoy::config::route::v3::RouteConfiguration route_config) {
  envoy::config::route::v3::RouteConfiguration clean_config = route_config;
  auto virtual_hosts = clean_config.mutable_virtual_hosts();
  std::for_each(virtual_hosts->begin(), virtual_hosts->end(),
                [](envoy::config::route::v3::VirtualHost& virtual_host) {
                  auto routes = virtual_host.mutable_routes();
                  for (int i = 0; i < routes->size();) {
                    if (routes->Get(i).has_filter_action()) {
                      routes->erase(routes->begin() + i);
                    } else {
                      ++i;
                    }
                  }
                });

  return clean_config;
}

// TODO(htuch): figure out how to generate via a genrule from config_impl_test the full corpus.
DEFINE_PROTO_FUZZER(const test::common::router::RouteTestCase& input) {
  static NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  static NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  try {
    TestUtility::validate(input);
    ConfigImpl config(cleanRouteConfig(input.config()), OptionalHttpFilters(), factory_context,
                      ProtobufMessage::getNullValidationVisitor(), true);
    auto headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.headers());
    auto route = config.route(headers, stream_info, input.random_value());
    if (route != nullptr && route->routeEntry() != nullptr) {
      route->routeEntry()->finalizeRequestHeaders(headers, stream_info, true);
    }
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Router
} // namespace Envoy

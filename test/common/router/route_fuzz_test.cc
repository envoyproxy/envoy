#include "envoy/api/v2/rds.pb.validate.h"

#include "common/router/config_impl.h"

#include "test/common/router/route_fuzz.pb.validate.h"
#include "test/fuzz/common.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Router {
namespace {

// Removes invalid headers from the RouteConfiguration as well as in each of the virtual hosts.
envoy::api::v2::RouteConfiguration
cleanRouteConfig(envoy::api::v2::RouteConfiguration route_config) {
  auto clean_config = route_config;
  auto virtual_hosts = clean_config.mutable_virtual_hosts();
  std::for_each(virtual_hosts->begin(), virtual_hosts->end(),
                [](envoy::api::v2::route::VirtualHost& virtual_host) {
                  auto routes = virtual_host.mutable_routes();
                  for (int i = 0; i < routes->size();) {
                    // Erase routes that use a regex matcher. This is deprecated and may cause
                    // crashes when wildcards are matched against very long headers. See
                    // https://github.com/envoyproxy/envoy/issues/7728.
                    if (routes->Get(i).match().path_specifier_case() ==
                        envoy::api::v2::route::RouteMatch::kRegex) {
                      routes->erase(routes->begin() + i);
                    }
                    ++i;
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
    ConfigImpl config(cleanRouteConfig(input.config()), factory_context,
                      ProtobufMessage::getNullValidationVisitor(), true);
    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(input.headers());
    // It's a precondition of routing that {:authority, :path, x-forwarded-proto} headers exists,
    // HCM enforces this.
    if (headers.Host() == nullptr) {
      headers.setHost("example.com");
    }
    if (headers.Path() == nullptr) {
      headers.setPath("/");
    }
    if (headers.ForwardedProto() == nullptr) {
      headers.setForwardedProto("http");
    }
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

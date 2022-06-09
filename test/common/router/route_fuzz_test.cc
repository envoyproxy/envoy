#include <functional>

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

// Limit the size of the input.
static const size_t MaxInputSize = 64 * 1024;

bool isUnsupportedRouteConfig(const envoy::config::route::v3::Route& route,
                              bool extended_check = false) {
  if (route.has_filter_action() || route.has_non_forwarding_action()) {
    return true;
  }
  if (extended_check && route.has_route() &&
      !(route.route().has_cluster_specifier_plugin() || route.route().has_cluster() ||
        route.route().has_cluster_header() || route.route().has_weighted_clusters())) {
    return true;
  }
  return false;
}

// Remove regex matching route configs.
envoy::config::route::v3::RouteConfiguration
cleanRouteConfig(envoy::config::route::v3::RouteConfiguration route_config) {
  envoy::config::route::v3::RouteConfiguration clean_config = route_config;
  auto virtual_hosts = clean_config.mutable_virtual_hosts();
  std::for_each(virtual_hosts->begin(), virtual_hosts->end(),
                [](envoy::config::route::v3::VirtualHost& virtual_host) {
                  auto routes = virtual_host.mutable_routes();
                  for (int i = 0; i < routes->size();) {
                    if (isUnsupportedRouteConfig(routes->Get(i))) {
                      routes->erase(routes->begin() + i);
                    } else {
                      ++i;
                    }
                  }
                });

  return clean_config;
}

// Check configuration for size and unimplemented/missing options.
bool validateConfig(const test::common::router::RouteTestCase& input) {
  const auto input_size = input.ByteSizeLong();
  if (input_size > MaxInputSize) {
    ENVOY_LOG_MISC(debug, "Input size {}kB exceeds {}kB ({}B > {}B).", input_size / 1024,
                   MaxInputSize / 1024, input_size, MaxInputSize);
    return false;
  }
  for (const auto& virtual_host : input.config().virtual_hosts()) {
    if (virtual_host.has_retry_policy_typed_config()) {
      ENVOY_LOG_MISC(debug, "retry_policy_typed_config: not implemented");
      return false;
    }
    if (virtual_host.has_matcher() && virtual_host.matcher().on_no_match().has_action() &&
        virtual_host.matcher().on_no_match().action().name() ==
            "type.googleapis.com/envoy.config.route.v3.Route") {
      envoy::config::route::v3::Route on_no_match_route_action_config;
      MessageUtil::unpackTo(virtual_host.matcher().on_no_match().action().typed_config(),
                            on_no_match_route_action_config);
      ENVOY_LOG_MISC(trace, "typed_config of virtual_host.matcher.on_no_match.action is: {}",
                     on_no_match_route_action_config.DebugString());
      if (isUnsupportedRouteConfig(on_no_match_route_action_config, true)) {
        ENVOY_LOG_MISC(debug, "matcher.on_no_match.action not sufficient for processing");
        return false;
      }
    }
  }
  return true;
}

// TODO(htuch): figure out how to generate via a genrule from config_impl_test the full corpus.
DEFINE_PROTO_FUZZER(const test::common::router::RouteTestCase& input) {
  if (!validateConfig(input)) {
    return;
  }

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

#include "envoy/api/v2/rds.pb.validate.h"

#include "common/router/config_impl.h"

#include "test/common/router/route_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Router {
namespace {

// A templated method to replace invalid characters in a protocol buffer that contains
// (request/response)_headers_to_(add/remove).
template <class T> T replaceInvalidHeaders(const T& config) {
  T clean_config = config;
  clean_config.mutable_request_headers_to_add()->CopyFrom(
      Fuzz::replaceInvalidHeaders(config.request_headers_to_add()));
  clean_config.mutable_response_headers_to_add()->CopyFrom(
      Fuzz::replaceInvalidHeaders(config.response_headers_to_add()));
  auto request_headers_to_remove = clean_config.mutable_request_headers_to_remove();
  std::for_each(request_headers_to_remove->begin(), request_headers_to_remove->end(),
                [](std::string& n) { n = Fuzz::replaceInvalidCharacters(n); });
  auto response_headers_to_remove = clean_config.mutable_response_headers_to_remove();
  std::for_each(response_headers_to_remove->begin(), response_headers_to_remove->end(),
                [](std::string& n) { n = Fuzz::replaceInvalidCharacters(n); });
  return clean_config;
}

// Removes invalid headers from the RouteConfiguration as well as in each of the virtual hosts.
envoy::api::v2::RouteConfiguration
cleanRouteConfig(envoy::api::v2::RouteConfiguration route_config) {
  // A route config contains a list of HTTP headers that should be added and/or removed to each
  // request and/or response the connection manager routes. This removes invalid characters the
  // headers.
  envoy::api::v2::RouteConfiguration clean_config =
      replaceInvalidHeaders<envoy::api::v2::RouteConfiguration>(route_config);
  auto internal_only_headers = clean_config.mutable_internal_only_headers();
  std::for_each(internal_only_headers->begin(), internal_only_headers->end(),
                [](std::string& n) { n = Fuzz::replaceInvalidCharacters(n); });
  auto virtual_hosts = clean_config.mutable_virtual_hosts();
  std::for_each(
      virtual_hosts->begin(), virtual_hosts->end(),
      [](envoy::api::v2::route::VirtualHost& virtual_host) {
        // Each virtual host in the routing configuration contains a list of headers to add and/or
        // remove from each request and response that get routed through it. This replaces invalid
        // header characters in these fields.
        virtual_host = replaceInvalidHeaders<envoy::api::v2::route::VirtualHost>(virtual_host);
        // Envoy can determine the cluster to route to by reading the HTTP header named by the
        // cluster_header from the request header. Because these cluster_headers are destined to be
        // added to a Header Map, we iterate through each route in and remove invalid characters
        // from their cluster headers.
        auto routes = virtual_host.mutable_routes();
        for (int i = 0; i < routes->size();) {
          // Erase routes that use a regex matcher. This is deprecated and may cause crashes when
          // wildcards are matched against very long headers.
          // See https://github.com/envoyproxy/envoy/issues/7728.
          if (routes->Get(i).match().path_specifier_case() ==
              envoy::api::v2::route::RouteMatch::kRegex) {
            routes->erase(routes->begin() + i);
          } else {
            if (routes->Get(i).has_route()) {
              routes->Mutable(i)->mutable_route()->set_cluster_header(
                  Fuzz::replaceInvalidCharacters(routes->Mutable(i)->route().cluster_header()));
              if (routes->Get(i).route().cluster_header().empty()) {
                routes->Mutable(i)->mutable_route()->set_cluster_header("not-empty");
              }
            }
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
    TestUtility::validate(input.config());
    ConfigImpl config(cleanRouteConfig(input.config()), factory_context,
                      ProtobufMessage::getNullValidationVisitor(), true);
    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(input.headers());
    // It's a precondition of routing that {:authority, :path, x-forwarded-proto} headers exists,
    // HCM enforces this.
    if (headers.Host() == nullptr) {
      headers.insertHost().value(std::string("example.com"));
    }
    if (headers.Path() == nullptr) {
      headers.insertPath().value(std::string("/"));
    }
    if (headers.ForwardedProto() == nullptr) {
      headers.insertForwardedProto().value(std::string("http"));
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

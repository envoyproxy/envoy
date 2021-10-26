#include "contrib/generic_proxy/filters/network/source/route_impl.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_config.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_route.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

SingleHeaderMatch::SingleHeaderMatch(
    const envoy::extensions::filters::network::generic_proxy::v3::HeaderMatch& header)
    : name_(header.name()), invert_match_(header.invert_match()) {
  if (header.has_string_match()) {
    string_.emplace(Envoy::Matchers::StringMatcherImpl(header.string_match()));
  }
}

bool SingleHeaderMatch::match(const GenericRequest& request) const {
  auto header = request.getByKey(name_);
  bool result = false;

  if (string_.has_value()) {
    result = header.has_value() ? string_->match(header.value()) : false;
  } else {
    result = header.has_value() == present_match_;
  }

  return result != invert_match_;
}

SingleRouteMatch ::SingleRouteMatch(
    const envoy::extensions::filters::network::generic_proxy::v3::RouteMatch& matcher) {
  if (matcher.has_path()) {
    path_.emplace(Envoy::Matchers::StringMatcherImpl(matcher.path()));
  }
  if (matcher.has_method()) {
    method_.emplace(Envoy::Matchers::StringMatcherImpl(matcher.method()));
  }

  for (const auto& header : matcher.headers()) {
    headers_.push_back({header});
  }
}

bool SingleRouteMatch::match(const GenericRequest& request) const {
  if (method_.has_value() && !method_->match(request.method())) {
    return false;
  }
  if (path_.has_value() && !path_->match(request.path())) {
    return false;
  }

  for (const auto& header_match : headers_) {
    if (!header_match.match(request)) {
      return false;
    }
  }
  return true;
}

RouteEntryImpl::RouteEntryImpl(
    const envoy::extensions::filters::network::generic_proxy::v3::Route& route,
    Envoy::Server::Configuration::FactoryContext& context)
    : route_match_(route.match()),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route, timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      retry_policy_(route.retry()), metadata_(route.metadata()), typed_metadata_(metadata_) {

  for (const auto& proto_filter_config : route.per_filter_config()) {
    auto& factory = Config::Utility::getAndCheckFactoryByName<NamedGenericFilterConfigFactory>(
        proto_filter_config.first);

    ProtobufTypes::MessagePtr message = factory.createEmptyRouteConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(proto_filter_config.second, ProtobufWkt::Struct(),
                                                  context.messageValidationVisitor(), *message);

    auto route_config = factory.createRouteSpecificFilterConfig(*message, context,
                                                                context.messageValidationVisitor());
    per_filter_configs_.emplace(proto_filter_config.first, std::move(route_config));
  }
}

RouteMatcherImpl::RouteMatcherImpl(
    const envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration& route_config,
    Envoy::Server::Configuration::FactoryContext& context)
    : name_(route_config.name()) {
  for (const auto& authority : route_config.config()) {
    auto routes = std::make_shared<std::vector<RouteEntryImplConstSharedPtr>>();
    for (const auto& route : authority.routes()) {
      routes->push_back(std::make_shared<RouteEntryImpl>(route, context));
    }
    if (routes->empty()) {
      continue;
    }
    for (const auto& authority_name : authority.authorities()) {
      if (authority_name.empty()) {
        throw EnvoyException("Empty authority name for generic proxy is not allowed");
      }
      if (routes_.count(authority_name) > 0) {
        throw EnvoyException(fmt::format(
            "Repeated authority name: {} for generic proxy is not allowed", authority_name));
      }
      routes_.emplace(authority_name, routes);
    }
  }
}

RouteEntryConstSharedPtr RouteMatcherImpl::routeEntry(const GenericRequest& request) const {
  auto iter = routes_.find(request.authority());
  if (iter == routes_.end()) {
    return nullptr;
  }

  for (auto& route : *iter->second) {
    if (route->match(request)) {
      return route;
    }
  }
  return nullptr;
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy

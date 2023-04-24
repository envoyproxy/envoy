#include "contrib/generic_proxy/filters/network/source/route.h"

#include <memory>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"

#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"
#include "interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

RouteEntryImpl::RouteEntryImpl(const ProtoRouteAction& route_action,
                               Envoy::Server::Configuration::ServerFactoryContext& context)
    : name_(route_action.name()), cluster_name_(route_action.cluster()),
      metadata_(route_action.metadata()) {

  for (const auto& proto_filter_config : route_action.per_filter_config()) {
    auto& factory = Config::Utility::getAndCheckFactoryByName<NamedFilterConfigFactory>(
        proto_filter_config.first);

    ProtobufTypes::MessagePtr message = factory.createEmptyRouteConfigProto();
    if (message == nullptr) {
      continue;
    }

    Envoy::Config::Utility::translateOpaqueConfig(proto_filter_config.second,
                                                  context.messageValidationVisitor(), *message);

    auto route_config = factory.createRouteSpecificFilterConfig(*message, context,
                                                                context.messageValidationVisitor());
    per_filter_configs_.emplace(proto_filter_config.first, std::move(route_config));
  }
}

Matcher::ActionFactoryCb RouteMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_action =
      MessageUtil::downcastAndValidate<const ProtoRouteAction&>(config, validation_visitor);
  auto route = std::make_shared<RouteEntryImpl>(route_action, context.factory_context);
  return [route]() { return std::make_unique<RouteMatchAction>(route); };
}
REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

VirtualHostImpl::VirtualHostImpl(const ProtoVirtualHost& virtual_host_config,
                                 Envoy::Server::Configuration::ServerFactoryContext& context, bool)
    : name_(virtual_host_config.name()) {
  RouteActionValidationVisitor validation_visitor;
  RouteActionContext action_context{context};

  Matcher::MatchTreeFactory<Request, RouteActionContext> factory(action_context, context,
                                                                 validation_visitor);

  matcher_ = factory.create(virtual_host_config.routes())();

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                     validation_visitor.errors()[0]));
  }
}

RouteEntryConstSharedPtr VirtualHostImpl::routeEntry(const Request& request) const {
  auto match = Matcher::evaluateMatch<Request>(*matcher_, request);

  if (match.result_) {
    auto action = match.result_();

    // The only possible action that can be used within the route matching context
    // is the RouteMatchAction, so this must be true.
    ASSERT(action->typeUrl() == RouteMatchAction::staticTypeUrl());
    ASSERT(dynamic_cast<RouteMatchAction*>(action.get()));
    const RouteMatchAction& route_action = static_cast<const RouteMatchAction&>(*action);

    return route_action.route();
  }

  ENVOY_LOG(debug, "failed to match incoming request: {}",
            static_cast<uint32_t>(match.match_state_));
  return nullptr;
}

RouteMatcherImpl::RouteMatcherImpl(const ProtoRouteConfiguration& route_config,
                                   Envoy::Server::Configuration::ServerFactoryContext& context,
                                   bool)
    : name_(route_config.name()) {

  RouteActionValidationVisitor validation_visitor;
  RouteActionContext action_context{context};

  Matcher::MatchTreeFactory<Request, RouteActionContext> factory(action_context, context,
                                                                 validation_visitor);

  matcher_ = factory.create(route_config.routes())();

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                     validation_visitor.errors()[0]));
  }
}

const VirtualHostImpl* RouteMatcherImpl::findWildcardVirtualHost(
    const std::string& host, const RouteMatcher::WildcardVirtualHosts& wildcard_virtual_hosts,
    RouteMatcher::SubstringFunction substring_function) const {
  // We do a longest wildcard match against the host that's passed in
  // (e.g. "foo-bar.baz.com" should match "*-bar.baz.com" before matching "*.baz.com" for suffix
  // wildcards). This is done by scanning the length => wildcards map looking for every wildcard
  // whose size is < length.
  for (const auto& iter : wildcard_virtual_hosts) {
    const uint32_t wildcard_length = iter.first;
    const auto& wildcard_map = iter.second;
    // >= because *.foo.com shouldn't match .foo.com.
    if (wildcard_length >= host.size()) {
      continue;
    }
    const auto& match = wildcard_map.find(substring_function(host, wildcard_length));
    if (match != wildcard_map.end()) {
      return match->second.get();
    }
  }
  return nullptr;
}

RouteEntryConstSharedPtr RouteMatcherImpl::routeEntry(const Request& request) const {
  auto match = Matcher::evaluateMatch<Request>(*matcher_, request);

  if (match.result_) {
    auto action = match.result_();

    // The only possible action that can be used within the route matching context
    // is the RouteMatchAction, so this must be true.
    ASSERT(action->typeUrl() == RouteMatchAction::staticTypeUrl());
    ASSERT(dynamic_cast<RouteMatchAction*>(action.get()));
    const RouteMatchAction& route_action = static_cast<const RouteMatchAction&>(*action);

    return route_action.route();
  }

  ENVOY_LOG(debug, "failed to match incoming request: {}",
            static_cast<uint32_t>(match.match_state_));
  return nullptr;
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

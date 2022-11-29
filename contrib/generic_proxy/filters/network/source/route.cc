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
    : cluster_name_(route_action.cluster()), metadata_(route_action.metadata()) {

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

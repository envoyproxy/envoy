#include "source/extensions/filters/network/meta_protocol_proxy/route.h"

#include <memory>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/route.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/config.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/route.h"

#include "interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

RouteEntryImpl::RouteEntryImpl(
    const envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteAction& route_action,
    Envoy::Server::Configuration::ServerFactoryContext& context)
    : cluster_name_(route_action.cluster()),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route_action, timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      metadata_(route_action.metadata()) {

  for (const auto& proto_filter_config : route_action.per_filter_config()) {
    auto& factory = Config::Utility::getAndCheckFactoryByName<NamedFilterConfigFactory>(
        proto_filter_config.first);

    ProtobufTypes::MessagePtr message = factory.createEmptyRouteConfigProto();
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
  const auto& route_action = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteAction&>(
      config, validation_visitor);
  auto route = std::make_shared<RouteEntryImpl>(route_action, context.factory_context);
  return [route]() { return std::make_unique<RouteMatchAction>(route); };
}
REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

RouteMatcherImpl::RouteMatcherImpl(
    const envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteConfiguration&
        route_config,
    Envoy::Server::Configuration::FactoryContext& context)
    : name_(route_config.name()) {

  RouteActionValidationVisitor validation_visitor;
  RouteActionContext action_context{context.getServerFactoryContext()};

  Matcher::MatchTreeFactory<Request, RouteActionContext> factory(
      action_context, context.getServerFactoryContext(), validation_visitor);

  matcher_ = factory.create(route_config.routes())();

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                     validation_visitor.errors()[0]));
  }
}

RouteEntryConstSharedPtr RouteMatcherImpl::routeEntry(const Request& request) const {
  auto match = Matcher::evaluateMatch<Request>(*matcher_, request);

  if (match.result_) {
    // The only possible action that can be used within the route matching context
    // is the RouteMatchAction, so this must be true.
    ASSERT(match.result_->typeUrl() == RouteMatchAction::staticTypeUrl());
    ASSERT(dynamic_cast<RouteMatchAction*>(match.result_.get()));
    const RouteMatchAction& route_action = static_cast<const RouteMatchAction&>(*match.result_);

    return route_action.route();
  }

  ENVOY_LOG(debug, "failed to match incoming request: {}", match.match_state_);
  return nullptr;
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

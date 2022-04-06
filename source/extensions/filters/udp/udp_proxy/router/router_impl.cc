#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.validate.h"

#include "source/common/common/empty_string.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

Matcher::ActionFactoryCb RouteMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::udp::udp_proxy::v3::Route&>(config, validation_visitor);
  const auto& cluster = route_config.cluster();

  // Emplace cluster names to context to get all cluster names.
  context.cluster_name_.emplace(cluster);

  return [cluster]() { return std::make_unique<RouteMatchAction>(cluster); };
}

REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

absl::Status RouteActionValidationVisitor::performDataInputValidation(
    const Matcher::DataInputFactory<Network::UdpMatchingData>&, absl::string_view) {
  return absl::OkStatus();
}

RouterImpl::RouterImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
                       Server::Configuration::ServerFactoryContext& factory_context) {
  xds::type::matcher::v3::Matcher matcher;
  if (config.has_cluster()) {
    // Convert deprecated cluster field into a matcher config.
    auto action = matcher.mutable_on_no_match()->mutable_action();
    action->set_name("route");
    envoy::extensions::filters::udp::udp_proxy::v3::Route route;
    route.set_cluster(config.cluster());
    action->mutable_typed_config()->PackFrom(route);
  } else {
    matcher = config.matcher();
  }

  RouteActionContext context{};
  RouteActionValidationVisitor validation_visitor;
  Matcher::MatchTreeFactory<Network::UdpMatchingData, RouteActionContext> factory(
      context, factory_context, validation_visitor);
  matcher_ = factory.create(matcher)();

  // All UDP network inputs should be accept, so there will be no error.
  ASSERT(validation_visitor.errors().empty());

  // Copy all clusters names.
  cluster_names_.insert(cluster_names_.end(), context.cluster_name_.begin(),
                        context.cluster_name_.end());
}

const std::string RouterImpl::route(const Network::Address::Instance& destination_address,
                                    const Network::Address::Instance& source_address) const {
  Network::Matching::UdpMatchingDataImpl data(destination_address, source_address);
  const auto& result = Matcher::evaluateMatch<Network::UdpMatchingData>(*matcher_, data);
  ASSERT(result.match_state_ == Matcher::MatchState::MatchComplete);
  if (result.result_) {
    return result.result_()->getTyped<RouteMatchAction>().cluster();
  }

  return EMPTY_STRING;
}

const std::vector<std::string>& RouterImpl::allClusterNames() const { return cluster_names_; }

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

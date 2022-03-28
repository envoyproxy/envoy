#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/route.pb.validate.h"

#include "source/common/common/empty_string.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

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

  // Emplace cluster names to context to get all cluster names
  context.cluster_name_.emplace(cluster);

  return [cluster]() { return std::make_unique<RouteMatchAction>(cluster); };
}

REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

absl::Status RouteActionValidationVisitor::performDataInputValidation(
    const Matcher::DataInputFactory<Network::UdpMatchingData>&, absl::string_view type_url) {
  static std::string source_ip_input_name = TypeUtil::descriptorFullNameToTypeUrl(
      envoy::extensions::matching::common_inputs::network::v3::SourceIPInput::descriptor()
          ->full_name());
  if (type_url == source_ip_input_name) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      fmt::format("Route table can only match on source IP, saw {}", type_url));
}

RouterImpl::RouterImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
                       Server::Configuration::ServerFactoryContext& factory_context) {
  if (config.has_cluster()) {
    cluster_ = config.cluster();
    cluster_names_.push_back(config.cluster());
  } else {
    RouteActionContext context{};
    RouteActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Network::UdpMatchingData, RouteActionContext> factory(
        context, factory_context, validation_visitor);
    matcher_ = factory.create(config.matcher())();

    if (!validation_visitor.errors().empty()) {
      // TODO(snowp): Output all violations.
      throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                       validation_visitor.errors()[0]));
    }

    // Copy all clusters names
    cluster_names_.insert(cluster_names_.end(), context.cluster_name_.begin(),
                          context.cluster_name_.end());
  }
}

const std::string RouterImpl::route(Network::Address::InstanceConstSharedPtr destination_address,
                                    Network::Address::InstanceConstSharedPtr source_address) const {
  if (cluster_.has_value()) {
    return cluster_.value();
  }

  if (source_address->ip()) {
    Network::Matching::UdpMatchingDataImpl data(destination_address, source_address);

    auto result = matcher_->match(data);
    if (result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (result.on_match_.has_value()) {
        return result.on_match_.value().action_cb_()->getTyped<RouteMatchAction>().cluster();
      }
    }
  }

  return EMPTY_STRING;
}

const std::vector<std::string>& RouterImpl::allClusterNames() const { return cluster_names_; }

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

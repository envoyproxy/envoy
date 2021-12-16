#include "source/common/rds/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Rds {

RouteConfigUpdateReceiverImpl::RouteConfigUpdateReceiverImpl(
    ConfigTraits& config_traits, ProtoTraits& proto_traits,
    Server::Configuration::ServerFactoryContext& factory_context)
    : config_traits_(config_traits), proto_traits_(proto_traits),
      time_source_(factory_context.timeSource()),
      route_config_proto_(proto_traits_.createEmptyProto()), last_config_hash_(0ull),
      config_(config_traits_.createNullConfig()) {}

void RouteConfigUpdateReceiverImpl::updateConfig(
    std::unique_ptr<Protobuf::Message>&& route_config_proto) {
  config_ = config_traits_.createConfig(*route_config_proto);
  // If the above create config doesn't raise exception, update the
  // other cached config entries.
  route_config_proto_ = std::move(route_config_proto);
}

void RouteConfigUpdateReceiverImpl::onUpdateCommon(const std::string& version_info) {
  last_config_version_ = version_info;
  last_updated_ = time_source_.systemTime();
  config_info_.emplace(RouteConfigProvider::ConfigInfo{*route_config_proto_, last_config_version_});
}

// Rds::RouteConfigUpdateReceiver
bool RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                const std::string& version_info) {
  uint64_t new_hash = getHash(rc);
  if (!checkHash(new_hash)) {
    return false;
  }
  updateConfig(proto_traits_.cloneProto(rc));
  updateHash(new_hash);
  onUpdateCommon(version_info);
  return true;
}

const std::string& RouteConfigUpdateReceiverImpl::routeConfigName() const {
  return proto_traits_.resourceName(*route_config_proto_);
}

absl::optional<RouteConfigProvider::ConfigInfo> RouteConfigUpdateReceiverImpl::configInfo() const {
  return config_info_;
}

} // namespace Rds
} // namespace Envoy

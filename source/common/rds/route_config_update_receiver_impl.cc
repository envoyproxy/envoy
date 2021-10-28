#include "source/common/rds/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Rds {

RouteConfigUpdateReceiverImpl::RouteConfigUpdateReceiverImpl(
    ConfigTraits& config_traits, Server::Configuration::ServerFactoryContext& factory_context)
    : config_traits_(config_traits), time_source_(factory_context.timeSource()),
      route_config_proto_(config_traits_.createProto()), last_config_hash_(0ull),
      config_(config_traits_.createConfig()) {}

bool RouteConfigUpdateReceiverImpl::updateHash(const Protobuf::Message& rc) {
  const uint64_t new_hash = MessageUtil::hash(rc);
  if (new_hash == last_config_hash_) {
    return false;
  }
  last_config_hash_ = new_hash;
  return true;
}

void RouteConfigUpdateReceiverImpl::updateConfig(
    std::unique_ptr<Protobuf::Message>&& route_config_proto) {
  config_ = config_traits_.createConfig(*route_config_proto);
  route_config_proto_ = std::move(route_config_proto);
}

void RouteConfigUpdateReceiverImpl::onUpdateCommon(const std::string& version_info) {
  last_config_version_ = version_info;
  last_updated_ = time_source_.systemTime();
  config_info_.emplace(RouteConfigProvider::ConfigInfo{*route_config_proto_, routeConfigName(),
                                                       last_config_version_});
}

// Rds::RouteConfigUpdateReceiver
bool RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                const std::string& version_info) {
  if (!updateHash(rc)) {
    return false;
  }
  updateConfig(config_traits_.cloneProto(rc));
  onUpdateCommon(version_info);
  return true;
}

const std::string& RouteConfigUpdateReceiverImpl::routeConfigName() const {
  return config_traits_.resourceName(*route_config_proto_);
}

absl::optional<RouteConfigProvider::ConfigInfo> RouteConfigUpdateReceiverImpl::configInfo() const {
  return config_info_;
}

} // namespace Rds
} // namespace Envoy

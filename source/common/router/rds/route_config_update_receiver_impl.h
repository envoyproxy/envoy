#pragma once

#include <string>

#include "envoy/router/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"

#include "source/common/router/rds/config_factory.h"

namespace Envoy {
namespace Router {
namespace Rds {

template <class RouteConfiguration, class Config>
class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver<RouteConfiguration, Config> {
public:
  RouteConfigUpdateReceiverImpl(Server::Configuration::ServerFactoryContext& factory_context,
                                ConfigFactory<RouteConfiguration, Config>& config_factory)
      : time_source_(factory_context.timeSource()),
        route_config_proto_(std::make_unique<RouteConfiguration>()), last_config_hash_(0ull),
        config_(config_factory.createConfig()), config_factory_(config_factory) {}

  bool updateHash(const RouteConfiguration& rc) {
    const uint64_t new_hash = MessageUtil::hash(rc);
    if (new_hash == last_config_hash_) {
      return false;
    }
    last_config_hash_ = new_hash;
    return true;
  }

  void updateConfig(std::unique_ptr<RouteConfiguration>&& route_config_proto) {
    config_ = config_factory_.createConfig(*route_config_proto);
    route_config_proto_ = std::move(route_config_proto);
  }

  void onUpdateCommon(const std::string& version_info) {
    last_config_version_ = version_info;
    last_updated_ = time_source_.systemTime();
    config_info_.emplace(typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo{
        *route_config_proto_, last_config_version_});
  }

  // Rds::RouteConfigUpdateReceiver
  bool onRdsUpdate(const RouteConfiguration& rc, const std::string& version_info) override {
    if (!updateHash(rc)) {
      return false;
    }
    updateConfig(std::make_unique<RouteConfiguration>(rc));
    onUpdateCommon(version_info);
    return true;
  }

  const std::string& routeConfigName() const override { return route_config_proto_->name(); }
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo>
  configInfo() const override {
    return config_info_;
  }
  const RouteConfiguration& protobufConfiguration() override { return *route_config_proto_; }
  std::shared_ptr<const Config> parsedConfiguration() const override { return config_; }
  SystemTime lastUpdated() const override { return last_updated_; }

private:
  TimeSource& time_source_;
  std::unique_ptr<RouteConfiguration> route_config_proto_;
  uint64_t last_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  absl::optional<typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo> config_info_;
  std::shared_ptr<const Config> config_;
  ConfigFactory<RouteConfiguration, Config>& config_factory_;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy

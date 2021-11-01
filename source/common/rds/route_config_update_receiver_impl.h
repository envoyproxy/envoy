#pragma once

#include <string>

#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Rds {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(ConfigTraits& config_traits,
                                Server::Configuration::ServerFactoryContext& factory_context);

  bool updateHash(const Protobuf::Message& rc);
  void updateConfig(std::unique_ptr<Protobuf::Message>&& route_config_proto);
  void onUpdateCommon(const std::string& version_info);

  // RouteConfigUpdateReceiver
  bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) override;

  const std::string& routeConfigName() const override;
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override;
  const Protobuf::Message& protobufConfiguration() override { return *route_config_proto_; }
  ConfigConstSharedPtr parsedConfiguration() const override { return config_; }
  SystemTime lastUpdated() const override { return last_updated_; }
  const ConfigTraits& configTraits() const override { return config_traits_; }

private:
  ConfigTraits& config_traits_;
  TimeSource& time_source_;
  ProtobufTypes::MessagePtr route_config_proto_;
  uint64_t last_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  ConfigConstSharedPtr config_;
};

} // namespace Rds
} // namespace Envoy

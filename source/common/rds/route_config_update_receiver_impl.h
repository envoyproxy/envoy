#pragma once

#include <string>

#include "envoy/rds/config_traits.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Rds {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(ConfigTraits& config_traits, ProtoTraits& proto_traits,
                                Server::Configuration::ServerFactoryContext& factory_context);

  uint64_t getHash(const Protobuf::Message& rc) const { return MessageUtil::hash(rc); }
  bool checkHash(uint64_t new_hash) const { return (new_hash != last_config_hash_); }
  void updateHash(uint64_t hash) { last_config_hash_ = hash; }
  void updateConfig(std::unique_ptr<Protobuf::Message>&& route_config_proto);
  void onUpdateCommon(const std::string& version_info);

  // RouteConfigUpdateReceiver
  bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) override;

  uint64_t configHash() const override { return last_config_hash_; }
  const absl::optional<RouteConfigProvider::ConfigInfo>& configInfo() const override;
  const Protobuf::Message& protobufConfiguration() const override { return *route_config_proto_; }
  ConfigConstSharedPtr parsedConfiguration() const override { return config_; }
  SystemTime lastUpdated() const override { return last_updated_; }

private:
  ConfigTraits& config_traits_;
  ProtoTraits& proto_traits_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  TimeSource& time_source_;
  ProtobufTypes::MessagePtr route_config_proto_;
  uint64_t last_config_hash_{0ull};
  SystemTime last_updated_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  ConfigConstSharedPtr config_;
};

} // namespace Rds
} // namespace Envoy

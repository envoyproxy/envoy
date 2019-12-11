#pragma once

#include <string>
#include <unordered_map>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"

#include "common/common/logger.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(TimeSource& time_source,
                                ProtobufMessage::ValidationVisitor& validation_visitor)
      : time_source_(time_source), last_config_hash_(0ull), last_vhds_config_hash_(0ul),
        validation_visitor_(validation_visitor), vhds_configuration_changed_(true) {}

  void initializeRdsVhosts(const envoy::api::v2::RouteConfiguration& route_configuration);
  void collectAliasesInUpdate(
      const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources);
  bool removeVhosts(std::map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(std::map<std::string, envoy::api::v2::route::VirtualHost>& vhosts,
                    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources);
  void rebuildRouteConfig(const std::map<std::string, envoy::api::v2::route::VirtualHost>& rds_vhosts,
                          const std::map<std::string, envoy::api::v2::route::VirtualHost>& vhds_vhosts,
                              envoy::api::v2::RouteConfiguration& route_config);
  bool aliasResolutionFailed(const envoy::api::v2::Resource& resource) const;
  void onUpdateCommon(const envoy::api::v2::RouteConfiguration& rc, const std::string& version_info);

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const envoy::api::v2::RouteConfiguration& rc,
                   const std::string& version_info) override;
  bool onVhdsUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  const std::string& routeConfigName() const override { return route_config_proto_.name(); }
  const std::string& configVersion() override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return config_info_;
  }
  bool vhdsConfigurationChanged() const override { return vhds_configuration_changed_; }
  const envoy::api::v2::RouteConfiguration& routeConfiguration() override {
    return route_config_proto_;
  }
  SystemTime lastUpdated() const override { return last_updated_; }
  const std::set<std::string>& aliasesInLastVhdsUpdate() override {
    return aliases_in_last_update_;
  }

private:
  TimeSource& time_source_;
  envoy::api::v2::RouteConfiguration route_config_proto_;
  uint64_t last_config_hash_;
  uint64_t last_vhds_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  std::map<std::string, envoy::api::v2::route::VirtualHost> rds_virtual_hosts_;
  std::map<std::string, envoy::api::v2::route::VirtualHost> vhds_virtual_hosts_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  std::set<std::string> aliases_in_last_update_;
  bool vhds_configuration_changed_;
};

} // namespace Router
} // namespace Envoy

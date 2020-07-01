#pragma once

#include <string>
#include <unordered_map>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/logger.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(TimeSource& time_source)
      : time_source_(time_source), last_config_hash_(0ull), last_vhds_config_hash_(0ul),
        vhds_configuration_changed_(true) {}

  void initializeRdsVhosts(const envoy::config::route::v3::RouteConfiguration& route_configuration);
  bool removeVhosts(std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
                    const VirtualHostRefVector& added_vhosts);
  void rebuildRouteConfig(
      const std::map<std::string, envoy::config::route::v3::VirtualHost>& rds_vhosts,
      const std::map<std::string, envoy::config::route::v3::VirtualHost>& vhds_vhosts,
      envoy::config::route::v3::RouteConfiguration& route_config);
  bool onDemandFetchFailed(const envoy::service::discovery::v3::Resource& resource) const;
  void onUpdateCommon(const envoy::config::route::v3::RouteConfiguration& rc,
                      const std::string& version_info);

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const envoy::config::route::v3::RouteConfiguration& rc,
                   const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    const std::set<std::string>& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  const std::string& routeConfigName() const override { return route_config_proto_.name(); }
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return config_info_;
  }
  bool vhdsConfigurationChanged() const override { return vhds_configuration_changed_; }
  const envoy::config::route::v3::RouteConfiguration& routeConfiguration() override {
    return route_config_proto_;
  }
  SystemTime lastUpdated() const override { return last_updated_; }
  const std::set<std::string>& resourceIdsInLastVhdsUpdate() override {
    return resource_ids_in_last_update_;
  }

private:
  TimeSource& time_source_;
  envoy::config::route::v3::RouteConfiguration route_config_proto_;
  uint64_t last_config_hash_;
  uint64_t last_vhds_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  std::map<std::string, envoy::config::route::v3::VirtualHost> rds_virtual_hosts_;
  std::map<std::string, envoy::config::route::v3::VirtualHost> vhds_virtual_hosts_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  std::set<std::string> resource_ids_in_last_update_;
  bool vhds_configuration_changed_;
};

} // namespace Router
} // namespace Envoy

#pragma once

#include <string>
#include <unordered_map>

#include "envoy/config/route/v3alpha/route.pb.h"
#include "envoy/config/route/v3alpha/route_components.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/service/discovery/v3alpha/discovery.pb.h"

#include "common/common/logger.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(TimeSource& time_source,
                                ProtobufMessage::ValidationVisitor& validation_visitor)
      : time_source_(time_source), last_config_hash_(0ull),
        validation_visitor_(validation_visitor) {}

  void
  initializeVhosts(const envoy::config::route::v3alpha::RouteConfiguration& route_configuration);
  void
  removeVhosts(std::unordered_map<std::string, envoy::config::route::v3alpha::VirtualHost>& vhosts,
               const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  void
  updateVhosts(std::unordered_map<std::string, envoy::config::route::v3alpha::VirtualHost>& vhosts,
               const Protobuf::RepeatedPtrField<envoy::service::discovery::v3alpha::Resource>&
                   added_resources);
  void rebuildRouteConfig(
      const std::unordered_map<std::string, envoy::config::route::v3alpha::VirtualHost>& vhosts,
      envoy::config::route::v3alpha::RouteConfiguration& route_config);

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const envoy::config::route::v3alpha::RouteConfiguration& rc,
                   const std::string& version_info) override;
  bool onVhdsUpdate(const Protobuf::RepeatedPtrField<envoy::service::discovery::v3alpha::Resource>&
                        added_resources,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  const std::string& routeConfigName() const override { return route_config_proto_.name(); }
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return config_info_;
  }
  const envoy::config::route::v3alpha::RouteConfiguration& routeConfiguration() override {
    return route_config_proto_;
  }
  SystemTime lastUpdated() const override { return last_updated_; }

private:
  TimeSource& time_source_;
  envoy::config::route::v3alpha::RouteConfiguration route_config_proto_;
  uint64_t last_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  std::unordered_map<std::string, envoy::config::route::v3alpha::VirtualHost> virtual_hosts_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Router
} // namespace Envoy

#pragma once

#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(Server::Configuration::ServerFactoryContext& factory_context,
                                const OptionalHttpFilters& optional_http_filters)
      : factory_context_(factory_context), time_source_(factory_context.timeSource()),
        route_config_proto_(std::make_unique<envoy::config::route::v3::RouteConfiguration>()),
        last_config_hash_(0ull), last_vhds_config_hash_(0ul),
        vhds_virtual_hosts_(
            std::make_unique<std::map<std::string, envoy::config::route::v3::VirtualHost>>()),
        vhds_configuration_changed_(true), optional_http_filters_(optional_http_filters) {}

  bool removeVhosts(std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
                    const Protobuf::RepeatedPtrField<std::string>& removed_vhost_names);
  bool updateVhosts(std::map<std::string, envoy::config::route::v3::VirtualHost>& vhosts,
                    const VirtualHostRefVector& added_vhosts);
  bool onDemandFetchFailed(const envoy::service::discovery::v3::Resource& resource) const;
  void onUpdateCommon(const std::string& version_info);

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const envoy::config::route::v3::RouteConfiguration& rc,
                   const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    const std::set<std::string>& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  const std::string& routeConfigName() const override { return route_config_proto_->name(); }
  const std::string& configVersion() const override { return last_config_version_; }
  uint64_t configHash() const override { return last_config_hash_; }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return config_info_;
  }
  bool vhdsConfigurationChanged() const override { return vhds_configuration_changed_; }
  const envoy::config::route::v3::RouteConfiguration& protobufConfiguration() override {
    return static_cast<const envoy::config::route::v3::RouteConfiguration&>(*route_config_proto_);
  }
  ConfigConstSharedPtr parsedConfiguration() const override { return config_; }
  SystemTime lastUpdated() const override { return last_updated_; }
  const std::set<std::string>& resourceIdsInLastVhdsUpdate() override {
    return resource_ids_in_last_update_;
  }

private:
  Server::Configuration::ServerFactoryContext& factory_context_;
  TimeSource& time_source_;
  std::unique_ptr<envoy::config::route::v3::RouteConfiguration> route_config_proto_;
  uint64_t last_config_hash_;
  uint64_t last_vhds_config_hash_;
  std::string last_config_version_;
  SystemTime last_updated_;
  std::map<std::string, envoy::config::route::v3::VirtualHost> rds_virtual_hosts_;
  std::unique_ptr<std::map<std::string, envoy::config::route::v3::VirtualHost>> vhds_virtual_hosts_;
  absl::optional<RouteConfigProvider::ConfigInfo> config_info_;
  std::set<std::string> resource_ids_in_last_update_;
  bool vhds_configuration_changed_;
  ConfigConstSharedPtr config_;
  const OptionalHttpFilters& optional_http_filters_;
};

} // namespace Router
} // namespace Envoy

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
#include "source/common/router/rds/config_factory.h"
#include "source/common/router/rds/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Router {

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(Server::Configuration::ServerFactoryContext& factory_context,
                                const OptionalHttpFilters& optional_http_filters)
      : config_factory_(factory_context, optional_http_filters),
        base_(factory_context, config_factory_), last_vhds_config_hash_(0ul),
        vhds_virtual_hosts_(
            std::make_unique<std::map<std::string, envoy::config::route::v3::VirtualHost>>()),
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

  // Router::RouteConfigUpdateReceiver
  bool onRdsUpdate(const envoy::config::route::v3::RouteConfiguration& rc,
                   const std::string& version_info) override;
  bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                    const std::set<std::string>& added_resource_ids,
                    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                    const std::string& version_info) override;
  const std::string& routeConfigName() const override { return base_.routeConfigName(); }
  const std::string& configVersion() const override { return base_.configVersion(); }
  uint64_t configHash() const override { return base_.configHash(); }
  absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const override {
    return base_.configInfo();
  }
  bool vhdsConfigurationChanged() const override { return vhds_configuration_changed_; }
  const envoy::config::route::v3::RouteConfiguration& protobufConfiguration() override {
    return base_.protobufConfiguration();
  }
  ConfigConstSharedPtr parsedConfiguration() const override { return base_.parsedConfiguration(); }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  const std::set<std::string>& resourceIdsInLastVhdsUpdate() override {
    return resource_ids_in_last_update_;
  }

private:
  class ConfigFactoryImpl
      : public Rds::ConfigFactory<envoy::config::route::v3::RouteConfiguration, Config> {
  public:
    ConfigFactoryImpl(Server::Configuration::ServerFactoryContext& factory_context,
                      const OptionalHttpFilters& optional_http_filters)
        : factory_context_(factory_context), optional_http_filters_(optional_http_filters) {}

    // Rds::ConfigFactory
    ConfigConstSharedPtr
    createConfig(const envoy::config::route::v3::RouteConfiguration& rc) const override;
    ConfigConstSharedPtr createConfig() const override;

  private:
    Server::Configuration::ServerFactoryContext& factory_context_;
    const OptionalHttpFilters optional_http_filters_;
  };
  ConfigFactoryImpl config_factory_;

  Rds::RouteConfigUpdateReceiverImpl<envoy::config::route::v3::RouteConfiguration, Config> base_;

  uint64_t last_vhds_config_hash_;
  std::map<std::string, envoy::config::route::v3::VirtualHost> rds_virtual_hosts_;
  std::unique_ptr<std::map<std::string, envoy::config::route::v3::VirtualHost>> vhds_virtual_hosts_;
  std::set<std::string> resource_ids_in_last_update_;
  bool vhds_configuration_changed_;
};

} // namespace Router
} // namespace Envoy

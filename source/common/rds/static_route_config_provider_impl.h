#pragma once

#include "envoy/rds/config_traits.h"
#include "envoy/rds/route_config_provider.h"
#include "envoy/server/factory_context.h"

#include "source/common/rds/route_config_provider_manager.h"

namespace Envoy {
namespace Rds {

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const Protobuf::Message& route_config_proto,
                                ConfigTraits& config_traits,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                RouteConfigProviderManager& route_config_provider_manager);

  ~StaticRouteConfigProviderImpl() override;

  // Router::RouteConfigProvider
  ConfigConstSharedPtr config() const override { return config_; }
  const absl::optional<ConfigInfo>& configInfo() const override { return config_info_; }
  SystemTime lastUpdated() const override { return last_updated_; }
  absl::Status onConfigUpdate() override { return absl::OkStatus(); }

private:
  ProtobufTypes::MessagePtr route_config_proto_;
  ConfigConstSharedPtr config_;
  SystemTime last_updated_;
  absl::optional<ConfigInfo> config_info_;
  RouteConfigProviderManager& route_config_provider_manager_;
};

} // namespace Rds
} // namespace Envoy

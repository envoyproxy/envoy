#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/rds/config_traits.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/static_route_config_provider_impl.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const envoy::config::route::v3::RouteConfiguration& config,
                                Rds::ConfigTraits& config_traits,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                Rds::RouteConfigProviderManager& route_config_provider_manager);
  ~StaticRouteConfigProviderImpl() override;

  // Router::RouteConfigProvider
  Rds::ConfigConstSharedPtr config() const override { return base_.config(); }
  const absl::optional<ConfigInfo>& configInfo() const override { return base_.configInfo(); }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  absl::Status onConfigUpdate() override { return base_.onConfigUpdate(); }
  ConfigConstSharedPtr configCast() const override;
  void requestVirtualHostsUpdate(const std::string&, Event::Dispatcher&,
                                 std::weak_ptr<Http::RouteConfigUpdatedCallback>) override {}

private:
  Rds::StaticRouteConfigProviderImpl base_;
  Rds::RouteConfigProviderManager& route_config_provider_manager_;
};

} // namespace Router
} // namespace Envoy

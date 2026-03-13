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
#include "source/common/router/vhds.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider,
                                      Logger::Loggable<Logger::Id::router> {
public:
  StaticRouteConfigProviderImpl(const envoy::config::route::v3::RouteConfiguration& config,
                                Rds::ConfigTraits& config_traits,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                Rds::RouteConfigProviderManager& route_config_provider_manager);
  ~StaticRouteConfigProviderImpl() override;

  // Router::RouteConfigProvider
  Rds::ConfigConstSharedPtr config() const override;
  const absl::optional<ConfigInfo>& configInfo() const override;
  SystemTime lastUpdated() const override;
  absl::Status onConfigUpdate() override;
  ConfigConstSharedPtr configCast() const override;
  void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) override;

private:
  // An internal struct that keeps the logic and objects required for VHDS
  // support encapsulated within. The VHDS update logic emulates a
  // RouteConfiguration update, and that will be achieved by the implemented
  // methods. This implies that the "static" route-configuration has now become
  // "dynamic" when VHDS is configured.
  struct VhdsContext {
    VhdsContext(const envoy::config::route::v3::RouteConfiguration& config,
                Server::Configuration::ServerFactoryContext& factory_context,
                StaticRouteConfigProviderImpl& parent,
                Rds::RouteConfigProviderManager& route_config_provider_manager);

    absl::Status onConfigUpdate();
    void requestVirtualHostsUpdate(const std::string& for_domain,
                                   Event::Dispatcher& thread_local_dispatcher,
                                   std::weak_ptr<Http::RouteConfigUpdatedCallback> cb);

    // Keeps a thread local RDS config that is updated when a VHDS update
    // arrives.
    struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
      ThreadLocalConfig(Rds::ConfigConstSharedPtr initial_config)
          : config_(std::move(initial_config)) {}
      Rds::ConfigConstSharedPtr config_;
    };

    struct UpdateOnDemandCallback {
      const std::string alias_;
      Event::Dispatcher& thread_local_dispatcher_;
      std::weak_ptr<Http::RouteConfigUpdatedCallback> cb_;
    };

    RouteConfigUpdatePtr config_update_info_;
    VhdsSubscriptionPtr vhds_subscription_;
    Server::Configuration::ServerFactoryContext& factory_context_;
    ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
    std::list<UpdateOnDemandCallback> config_update_callbacks_;
    // A flag used to determine if this instance of StaticRouteConfigProviderImpl hasn't been
    // deallocated. Please also see a comment in requestVirtualHostsUpdate() method implementation.
    std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
  };

  Rds::StaticRouteConfigProviderImpl base_;
  Rds::RouteConfigProviderManager& route_config_provider_manager_;
  std::unique_ptr<VhdsContext> vhds_context_;
};

} // namespace Router
} // namespace Envoy

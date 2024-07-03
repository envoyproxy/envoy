#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/route_config_update_receiver.h"
#include "envoy/server/admin.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/common/proto_traits_impl.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_provider_manager.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"
#include "source/common/router/route_provider_manager.h"
#include "source/common/router/vhds.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Router {

// For friend class declaration in RdsRouteConfigSubscription.
class ScopedRdsConfigSubscription;

/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */

class RdsRouteConfigSubscription : public Rds::RdsRouteConfigSubscription {
public:
  RdsRouteConfigSubscription(
      RouteConfigUpdatePtr&& config_update,
      Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
      const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
      const uint64_t manager_identifier,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Rds::RouteConfigProviderManager& route_config_provider_manager);
  ~RdsRouteConfigSubscription() override;

  RouteConfigUpdatePtr& routeConfigUpdate() { return config_update_info_; }
  void updateOnDemand(const std::string& aliases);
  void maybeCreateInitManager(const std::string& version_info,
                              std::unique_ptr<Init::ManagerImpl>& init_manager,
                              std::unique_ptr<Cleanup>& resume_rds);

private:
  absl::Status beforeProviderUpdate(std::unique_ptr<Init::ManagerImpl>& noop_init_manager,
                                    std::unique_ptr<Cleanup>& resume_rds) override;
  void afterProviderUpdate() override;

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) {
    return update_callback_manager_.add(callback);
  }

  VhdsSubscriptionPtr vhds_subscription_;
  RouteConfigUpdatePtr config_update_info_;
  Common::CallbackManager<> update_callback_manager_;

  // Access to addUpdateCallback
  friend class ScopedRdsConfigSubscription;
};

using RdsRouteConfigSubscriptionSharedPtr = std::shared_ptr<RdsRouteConfigSubscription>;

struct UpdateOnDemandCallback {
  const std::string alias_;
  Event::Dispatcher& thread_local_dispatcher_;
  std::weak_ptr<Http::RouteConfigUpdatedCallback> cb_;
};

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   Logger::Loggable<Logger::Id::router> {
public:
  RdsRouteConfigProviderImpl(RdsRouteConfigSubscriptionSharedPtr&& subscription,
                             Server::Configuration::ServerFactoryContext& factory_context);

  RdsRouteConfigSubscription& subscription();

  // Router::RouteConfigProvider
  Rds::ConfigConstSharedPtr config() const override { return base_.config(); }
  const absl::optional<ConfigInfo>& configInfo() const override { return base_.configInfo(); }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }

  absl::Status onConfigUpdate() override;
  ConfigConstSharedPtr configCast() const override;
  void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) override;

private:
  Rds::RdsRouteConfigProviderImpl base_;

  RouteConfigUpdatePtr& config_update_info_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  std::list<UpdateOnDemandCallback> config_update_callbacks_;
  // A flag used to determine if this instance of RdsRouteConfigProviderImpl hasn't been
  // deallocated. Please also see a comment in requestVirtualHostsUpdate() method implementation.
  std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

using RdsRouteConfigProviderImplSharedPtr = std::shared_ptr<RdsRouteConfigProviderImpl>;

class RdsFactoryImpl : public RdsFactory {
public:
  std::string name() const override { return "envoy.rds_factory.default"; }
  virtual RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager, ProtoTraitsImpl& proto_traits,
      Rds::RouteConfigProviderManager& manager) override;
};

DECLARE_FACTORY(RdsFactoryImpl);

} // namespace Router
} // namespace Envoy

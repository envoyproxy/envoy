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
#include "source/common/router/rds/rds_route_config_provider_impl.h"
#include "source/common/router/rds/rds_route_config_subscription.h"
#include "source/common/router/rds/route_config_provider_manager_impl.h"
#include "source/common/router/rds/route_config_update_receiver_impl.h"
#include "source/common/router/rds/static_route_config_provider_impl.h"
#include "source/common/router/vhds.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Router {

// For friend class declaration in RdsRouteConfigSubscription.
class ScopedRdsConfigSubscription;

/**
 * Route configuration provider utilities.
 */
class RouteConfigProviderUtil {
public:
  /**
   * @return RouteConfigProviderSharedPtr a new route configuration provider based on the supplied
   * proto configuration. Notes the provider object could be shared among multiple listeners.
   */
  static RouteConfigProviderSharedPtr create(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          config,
      Server::Configuration::ServerFactoryContext& factory_context,
      ProtobufMessage::ValidationVisitor& validator, Init::Manager& init_manager,
      const std::string& stat_prefix, RouteConfigProviderManager& route_config_provider_manager);
};

class RouteConfigProviderManagerImpl;
using ConfigFactory = Rds::ConfigFactory<envoy::config::route::v3::RouteConfiguration, Config>;

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
class StaticRouteConfigProviderImpl : public RouteConfigProvider {
public:
  StaticRouteConfigProviderImpl(const envoy::config::route::v3::RouteConfiguration& config,
                                const RouteConfigProviderManager::OptionalHttpFilters& http_filters,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                ProtobufMessage::ValidationVisitor& validator,
                                RouteConfigProviderManagerImpl& route_config_provider_manager);
  ~StaticRouteConfigProviderImpl() override;

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return base_.config(); }
  absl::optional<ConfigInfo> configInfo() const override { return base_.configInfo(); }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }
  void onConfigUpdate() override { base_.onConfigUpdate(); }
  void validateConfig(const envoy::config::route::v3::RouteConfiguration&) const override {}
  void requestVirtualHostsUpdate(const std::string&, Event::Dispatcher&,
                                 std::weak_ptr<Http::RouteConfigUpdatedCallback>) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  Rds::StaticRouteConfigProviderImpl<envoy::config::route::v3::RouteConfiguration, Config> base_;
  RouteConfigProviderManagerImpl& route_config_provider_manager_;
};

/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */

class RdsRouteConfigSubscription
    : public Rds::RdsRouteConfigSubscription<envoy::config::route::v3::RouteConfiguration, Config> {
public:
  RdsRouteConfigSubscription(
      RouteConfigUpdateReceiver* config_update,
      const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
      const uint64_t manager_identifier,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      RouteConfigProviderManagerImpl& route_config_provider_manager);

  absl::optional<RouteConfigProvider*>& routeConfigProvider() { return route_config_provider_opt_; }
  RouteConfigUpdateReceiver* routeConfigUpdate() { return config_update_info_; }
  void updateOnDemand(const std::string& aliases);
  void maybeCreateInitManager(const std::string& version_info,
                              std::unique_ptr<Init::ManagerImpl>& init_manager,
                              std::unique_ptr<Cleanup>& resume_rds);

private:
  void beforeProviderUpdate() override;
  void afterProviderUpdate() override;

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addUpdateCallback(std::function<void()> callback) {
    return update_callback_manager_.add(callback);
  }

  absl::optional<RouteConfigProvider*> route_config_provider_opt_;
  // The pointer is owned by the base class, here it is just stored as raw pointer to avoid
  // downcasting.
  RouteConfigUpdateReceiver* config_update_info_;

  VhdsSubscriptionPtr vhds_subscription_;
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
  RdsRouteConfigProviderImpl(RdsRouteConfigSubscription* subscription,
                             Server::Configuration::ServerFactoryContext& factory_context,
                             ConfigFactory& config_factory);

  ~RdsRouteConfigProviderImpl() override;

  RdsRouteConfigSubscription& subscription() { return *subscription_; }

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return base_.config(); }
  absl::optional<ConfigInfo> configInfo() const override { return base_.configInfo(); }
  SystemTime lastUpdated() const override { return base_.lastUpdated(); }

  void onConfigUpdate() override;
  void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) override;
  void validateConfig(const envoy::config::route::v3::RouteConfiguration& config) const override;

private:
  Rds::RdsRouteConfigProviderImpl<envoy::config::route::v3::RouteConfiguration, Config> base_;

  // The pointer is owned by base_, here it is just stored as raw pointer to avoid downcasting.
  RdsRouteConfigSubscription* subscription_;

  RouteConfigUpdateReceiver* config_update_info_;
  ConfigFactory& config_factory_;

  Server::Configuration::ServerFactoryContext& factory_context_;
  std::list<UpdateOnDemandCallback> config_update_callbacks_;
  // A flag used to determine if this instance of RdsRouteConfigProviderImpl hasn't been
  // deallocated. Please also see a comment in requestVirtualHostsUpdate() method implementation.
  std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
};

using RdsRouteConfigProviderImplSharedPtr = std::shared_ptr<RdsRouteConfigProviderImpl>;

class RouteConfigProviderManagerImpl
    : public RouteConfigProviderManager,
      public Singleton::Instance,
      public Rds::RouteConfigProviderManagerImpl<envoy::config::route::v3::RouteConfiguration,
                                                 Config> {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
      const OptionalHttpFilters& optional_http_filters,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) override;

  RouteConfigProviderPtr
  createStaticRouteConfigProvider(const envoy::config::route::v3::RouteConfiguration& route_config,
                                  const OptionalHttpFilters& optional_http_filters,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  ProtobufMessage::ValidationVisitor& validator) override;
};

using RouteConfigProviderManagerImplPtr = std::unique_ptr<RouteConfigProviderManagerImpl>;

} // namespace Router
} // namespace Envoy

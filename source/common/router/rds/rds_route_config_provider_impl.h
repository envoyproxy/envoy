#pragma once

#include <memory>
#include <string>

#include "envoy/router/rds/route_config_provider.h"
#include "envoy/router/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/router/rds/config_factory.h"
#include "source/common/router/rds/rds_route_config_subscription.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
template <class RouteConfiguration, class Config>
class RdsRouteConfigProviderImpl : public RouteConfigProvider<RouteConfiguration, Config>,
                                   Logger::Loggable<Logger::Id::router> {
public:
  RdsRouteConfigProviderImpl(
      RdsRouteConfigSubscriptionSharedPtr<RouteConfiguration, Config>&& subscription,
      Server::Configuration::ServerFactoryContext& factory_context,
      ConfigFactory<RouteConfiguration, Config>& config_factory)
      : subscription_(std::move(subscription)),
        config_update_info_(subscription_->routeConfigUpdate()),
        tls_(factory_context.threadLocal()) {

    std::shared_ptr<const Config> initial_config;
    if (config_update_info_->configInfo().has_value()) {
      initial_config =
          config_factory.createConfig(subscription_->routeConfigUpdate()->protobufConfiguration());
    } else {
      initial_config = config_factory.createConfig();
    }

    tls_.set([initial_config](Event::Dispatcher&) {
      return std::make_shared<ThreadLocalConfig>(initial_config);
    });
    // It should be 1:1 mapping due to shared rds config.
    ASSERT(!subscription_->routeConfigProvider().has_value());
    subscription_->routeConfigProvider().emplace(this);
  }

  ~RdsRouteConfigProviderImpl() override {
    ASSERT(subscription_->routeConfigProvider().has_value());
    subscription_->routeConfigProvider().reset();
  }

  RdsRouteConfigSubscription<RouteConfiguration, Config>& subscription() { return *subscription_; }

  // Rds::RouteConfigProvider
  std::shared_ptr<const Config> config() override { return tls_->config_; }

  absl::optional<typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo>
  configInfo() const override {
    return config_update_info_->configInfo();
  }
  SystemTime lastUpdated() const override { return config_update_info_->lastUpdated(); }
  void onConfigUpdate() override {
    tls_.runOnAllThreads(
        [new_config = config_update_info_->parsedConfiguration()](OptRef<ThreadLocalConfig> tls) {
          printf("XXXX %ld tls\n", syscall(SYS_gettid));
          tls->config_ = new_config;
          printf("XXXX tls end\n");
        });
  }
  void validateConfig(const RouteConfiguration&) const override {}

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(std::shared_ptr<const Config> initial_config)
        : config_(std::move(initial_config)) {}
    std::shared_ptr<const Config> config_;
  };

  RdsRouteConfigSubscriptionSharedPtr<RouteConfiguration, Config> subscription_;
  RouteConfigUpdatePtr<RouteConfiguration, Config>& config_update_info_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy

#pragma once

#include <memory>
#include <string>

#include "envoy/rds/route_config_provider.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/rds/rds_route_config_subscription.h"

namespace Envoy {
namespace Rds {

/**
 * Implementation of RouteConfigProvider that fetches the route configuration dynamically using
 * the subscription.
 */
class RdsRouteConfigProviderImpl : public RouteConfigProvider,
                                   Logger::Loggable<Logger::Id::router> {
public:
  RdsRouteConfigProviderImpl(RdsRouteConfigSubscriptionSharedPtr&& subscription,
                             Server::Configuration::ServerFactoryContext& factory_context);

  ~RdsRouteConfigProviderImpl() override;

  RdsRouteConfigSubscription& subscription() { return *subscription_; }

  // RouteConfigProvider
  ConfigConstSharedPtr config() const override { return tls_->config_; }

  const absl::optional<ConfigInfo>& configInfo() const override;
  SystemTime lastUpdated() const override { return config_update_info_->lastUpdated(); }
  absl::Status onConfigUpdate() override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(std::shared_ptr<const Config> initial_config)
        : config_(std::move(initial_config)) {}
    ConfigConstSharedPtr config_;
  };

  RdsRouteConfigSubscriptionSharedPtr subscription_;
  RouteConfigUpdatePtr& config_update_info_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
};

} // namespace Rds
} // namespace Envoy

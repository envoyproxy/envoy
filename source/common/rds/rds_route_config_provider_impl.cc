#include "source/common/rds/rds_route_config_provider_impl.h"

namespace Envoy {
namespace Rds {

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::ServerFactoryContext& factory_context)
    : subscription_(std::move(subscription)),
      config_update_info_(subscription_->routeConfigUpdate()), tls_(factory_context.threadLocal()) {

  auto initial_config = config_update_info_->parsedConfiguration();
  ASSERT(initial_config);
  tls_.set([initial_config](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
  // It should be 1:1 mapping due to shared rds config.
  ASSERT(subscription_->routeConfigProvider() == nullptr);
  subscription_->routeConfigProvider() = this;
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  ASSERT(subscription_->routeConfigProvider() != nullptr);
  subscription_->routeConfigProvider() = nullptr;
}

const absl::optional<RouteConfigProvider::ConfigInfo>&
RdsRouteConfigProviderImpl::configInfo() const {
  return config_update_info_->configInfo();
}

absl::Status RdsRouteConfigProviderImpl::onConfigUpdate() {
  tls_.runOnAllThreads([new_config = config_update_info_->parsedConfiguration()](
                           OptRef<ThreadLocalConfig> tls) { tls->config_ = new_config; });
  return absl::OkStatus();
}

} // namespace Rds
} // namespace Envoy

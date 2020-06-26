#include "common/router/filter_config_discovery_impl.h"

#include "common/grpc/common.h"

namespace Envoy {
namespace Router {

FilterConfigSubscription::FilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix)
    : Envoy::Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          config_source.resource_api_version()),
      filter_config_name_(name), factory_context_(factory_context),
      validator_(factory_context.messageValidationContext().dynamicValidationVisitor()),
      parent_init_target_(fmt::format("FilterConfigSubscription init {}", filter_config_name_),
                          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(fmt::format("RDS local-init-watcher {}", filter_config_name_),
                          [this]() { parent_init_target_.ready(); }),
      local_init_target_(
          fmt::format("FilterConfigSubscription local-init-target {}", filter_config_name_),
          [this]() { subscription_->start({filter_config_name_}); }),
      local_init_manager_(fmt::format("FilterConfigDS local-init-manager {}", filter_config_name_)),
      scope_(factory_context.scope().createScope(stat_prefix + "filter_config_discovery." +
                                                 filter_config_name_ + ".")) {
  //      stat_prefix_(stat_prefix), stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_))})
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this);
  local_init_manager_.add(local_init_target_);
  // TODO: figure out interface to HCM
  // config_update_info_ =
  //    std::make_unique<RouteConfigUpdateReceiverImpl>(factory_context.timeSource(), validator_);
  //    }
  // TODO: make local init target ready on the first update
}

FilterConfigSubscription::~FilterConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();
}

DynamicFilterConfigProviderImpl::DynamicFilterConfigProviderImpl(
    FilterConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::ServerFactoryContext& factory_context)
    : subscription_(std::move(subscription)), factory_context_(factory_context),
      tls_(factory_context.threadLocal().allocateSlot()) {}

} // namespace Router
} // namespace Envoy

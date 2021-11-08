#include "source/common/rds/route_config_provider_manager_impl.h"

namespace Envoy {
namespace Rds {

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(
    Server::Admin& admin, const std::string& config_tracker_key)
    : config_tracker_entry_(admin.getConfigTracker().add(
          config_tracker_key,
          [this](const Matchers::StringMatcher& matcher) { return dumpRouteConfigs(matcher); })) {
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

void RouteConfigProviderManagerImpl::eraseStaticProvider(RouteConfigProvider* provider) {
  static_route_config_providers_.erase(provider);
}

void RouteConfigProviderManagerImpl::eraseDynamicProvider(int64_t manager_identifier) {
  dynamic_route_config_providers_.erase(manager_identifier);
}

std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
RouteConfigProviderManagerImpl::dumpRouteConfigs(
    const Matchers::StringMatcher& name_matcher) const {
  auto config_dump = std::make_unique<envoy::admin::v3::RoutesConfigDump>();

  for (const auto& element : dynamic_route_config_providers_) {
    const auto provider = element.second.first.lock();
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    ASSERT(provider);

    if (provider->configInfo()) {
      if (!name_matcher.match(provider->configInfo().value().name_)) {
        continue;
      }
      auto* dynamic_config = config_dump->mutable_dynamic_route_configs()->Add();
      dynamic_config->set_version_info(provider->configInfo().value().version_);
      dynamic_config->mutable_route_config()->PackFrom(provider->configInfo().value().config_);
      TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : static_route_config_providers_) {
    ASSERT(provider->configInfo());
    if (!name_matcher.match(provider->configInfo().value().name_)) {
      continue;
    }
    auto* static_config = config_dump->mutable_static_route_configs()->Add();
    static_config->mutable_route_config()->PackFrom(provider->configInfo().value().config_);
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *static_config->mutable_last_updated());
  }

  return config_dump;
}

void RouteConfigProviderManagerImpl::insertStaticProvider(RouteConfigProvider* provider) {
  static_route_config_providers_.insert(provider);
}

void RouteConfigProviderManagerImpl::insertDynamicProvider(int64_t manager_identifier,
                                                           RouteConfigProviderSharedPtr provider,
                                                           const Init::Target* init_target,
                                                           Init::Manager& init_manager) {
  init_manager.add(*init_target);
  dynamic_route_config_providers_.insert(
      {manager_identifier,
       std::make_pair(std::weak_ptr<RouteConfigProvider>(provider), init_target)});
}

RouteConfigProviderSharedPtr
RouteConfigProviderManagerImpl::reuseDynamicProvider(uint64_t manager_identifier,
                                                     Init::Manager& init_manager,
                                                     const std::string& route_config_name) {
  auto it = dynamic_route_config_providers_.find(manager_identifier);
  if (it == dynamic_route_config_providers_.end()) {
    return RouteConfigProviderSharedPtr();
  }
  // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
  // in the RdsRouteConfigSubscription destructor, and the single threaded nature
  // of this code, locking the weak_ptr will not fail.
  auto existing_provider = it->second.first.lock();
  RELEASE_ASSERT(existing_provider != nullptr,
                 absl::StrCat("cannot find subscribed rds resource ", route_config_name));
  init_manager.add(*it->second.second);
  return existing_provider;
}

} // namespace Rds
} // namespace Envoy

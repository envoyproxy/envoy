#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/init/manager.h"
#include "envoy/init/target.h"
#include "envoy/server/admin.h"

#include "source/common/common/matchers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/rds/route_config_provider_manager.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Router {
namespace Rds {

template <class RouteConfiguration, class Config>
class RouteConfigProviderManagerImpl
    : public RouteConfigProviderManager<RouteConfiguration, Config> {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin) {
    config_tracker_entry_ =
        admin.getConfigTracker().add("routes", [this](const Matchers::StringMatcher& matcher) {
          return dumpRouteConfigs(matcher);
        });
    // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
    // from us, since the returned entry will be nullptr if the key already exists.
    RELEASE_ASSERT(config_tracker_entry_, "");
  }

  void eraseStaticProvider(RouteConfigProvider<RouteConfiguration, Config>* provider) override {
    static_route_config_providers_.erase(provider);
  }

  void eraseDynamicProvider(int64_t manager_identifier) override {
    dynamic_route_config_providers_.erase(manager_identifier);
  }

  std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
  dumpRouteConfigs(const Matchers::StringMatcher& name_matcher) const {
    auto config_dump = std::make_unique<envoy::admin::v3::RoutesConfigDump>();

    for (const auto& element : dynamic_route_config_providers_) {
      const auto provider = element.second.first.lock();
      // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
      // in the RdsRouteConfigSubscription destructor, and the single threaded nature
      // of this code, locking the weak_ptr will not fail.
      ASSERT(provider);

      if (provider->configInfo()) {
        if (!name_matcher.match(provider->configInfo().value().config_.name())) {
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
      if (!name_matcher.match(provider->configInfo().value().config_.name())) {
        continue;
      }
      auto* static_config = config_dump->mutable_static_route_configs()->Add();
      static_config->mutable_route_config()->PackFrom(provider->configInfo().value().config_);
      TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                            *static_config->mutable_last_updated());
    }

    return config_dump;
  }

protected:
  void insertStaticProvider(RouteConfigProvider<RouteConfiguration, Config>* provider) {
    static_route_config_providers_.insert(provider);
  }

  void insertDynamicProvider(int64_t manager_identifier,
                             RouteConfigProviderSharedPtr<RouteConfiguration, Config> provider,
                             const Init::Target* init_target) {
    dynamic_route_config_providers_.insert(
        {manager_identifier,
         std::make_pair(std::weak_ptr<RouteConfigProvider<RouteConfiguration, Config>>(provider),
                        init_target)});
  }

  RouteConfigProviderSharedPtr<RouteConfiguration, Config>
  reuseDynamicProvider(uint64_t manager_identifier, Init::Manager& init_manager,
                       const std::string& route_config_name) {
    auto it = dynamic_route_config_providers_.find(manager_identifier);
    if (it == dynamic_route_config_providers_.end()) {
      return RouteConfigProviderSharedPtr<RouteConfiguration, Config>();
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

private:
  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  absl::node_hash_map<uint64_t,
                      std::pair<std::weak_ptr<RouteConfigProvider<RouteConfiguration, Config>>,
                                const Init::Target*>>
      dynamic_route_config_providers_;
  absl::node_hash_set<RouteConfigProvider<RouteConfiguration, Config>*>
      static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy

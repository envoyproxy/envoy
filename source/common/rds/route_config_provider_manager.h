#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/init/manager.h"
#include "envoy/init/target.h"
#include "envoy/rds/config_traits.h"
#include "envoy/rds/route_config_provider.h"
#include "envoy/server/admin.h"

#include "source/common/common/matchers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Rds {

class RouteConfigProviderManager {
public:
  RouteConfigProviderManager(OptRef<Server::Admin> admin, const std::string& config_tracker_key,
                             ProtoTraits& proto_traits);

  void eraseStaticProvider(RouteConfigProvider* provider);
  void eraseDynamicProvider(uint64_t manager_identifier);

  ProtoTraits& protoTraits() { return proto_traits_; }

  std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
  dumpRouteConfigs(const Matchers::StringMatcher& name_matcher) const;

  RouteConfigProviderPtr
  addStaticProvider(std::function<RouteConfigProviderPtr()> create_static_provider);

  template <class RdsConfig>
  RouteConfigProviderSharedPtr
  addDynamicProvider(const RdsConfig& rds, const std::string& route_config_name,
                     Init::Manager& init_manager,
                     std::function<std::pair<RouteConfigProviderSharedPtr, const Init::Target*>(
                         uint64_t manager_identifier)>
                         create_dynamic_provider) {

    uint64_t manager_identifier;

    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.normalize_rds_provider_config")) {
      // Normalize the config_source part of the passed config. Some parts of the config_source
      // do not affect selection of the RDS provider. They will be cleared (zeroed) and restored
      // after calculating hash.
      // Since rds is passed as const, the constness must be casted away before modifying rds.
      auto* orig_initial_timeout =
          const_cast<RdsConfig&>(rds).mutable_config_source()->release_initial_fetch_timeout();
      manager_identifier = MessageUtil::hash(rds);
      const_cast<RdsConfig&>(rds).mutable_config_source()->set_allocated_initial_fetch_timeout(
          orig_initial_timeout);

    } else {
      manager_identifier = MessageUtil::hash(rds);
    }

    auto existing_provider =
        reuseDynamicProvider(manager_identifier, init_manager, route_config_name);

    if (existing_provider) {
      return existing_provider;
    }
    auto new_provider = create_dynamic_provider(manager_identifier);
    init_manager.add(*new_provider.second);
    dynamic_route_config_providers_.insert({manager_identifier, new_provider});
    return new_provider.first;
  }

private:
  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  absl::node_hash_map<uint64_t, std::pair<std::weak_ptr<RouteConfigProvider>, const Init::Target*>>
      dynamic_route_config_providers_;
  absl::node_hash_set<RouteConfigProvider*> static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  ProtoTraits& proto_traits_;

  RouteConfigProviderSharedPtr reuseDynamicProvider(uint64_t manager_identifier,
                                                    Init::Manager& init_manager,
                                                    const std::string& route_config_name);
};

} // namespace Rds
} // namespace Envoy

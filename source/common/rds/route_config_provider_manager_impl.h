#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/init/manager.h"
#include "envoy/init/target.h"
#include "envoy/server/admin.h"

#include "source/common/common/matchers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/rds/route_config_provider_manager.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Rds {

class RouteConfigProviderManagerImpl : public RouteConfigProviderManager {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  void eraseStaticProvider(RouteConfigProvider* provider) override;

  void eraseDynamicProvider(int64_t manager_identifier) override;

  std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
  dumpRouteConfigs(const Matchers::StringMatcher& name_matcher) const;

protected:
  void insertStaticProvider(RouteConfigProvider* provider);
  void insertDynamicProvider(int64_t manager_identifier, RouteConfigProviderSharedPtr provider,
                             const Init::Target* init_target, Init::Manager& init_manager);

  RouteConfigProviderSharedPtr reuseDynamicProvider(uint64_t manager_identifier,
                                                    Init::Manager& init_manager,
                                                    const std::string& route_config_name);

private:
  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  absl::node_hash_map<uint64_t, std::pair<std::weak_ptr<RouteConfigProvider>, const Init::Target*>>
      dynamic_route_config_providers_;
  absl::node_hash_set<RouteConfigProvider*> static_route_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
};

} // namespace Rds
} // namespace Envoy

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

  using RouteConfigProviderCb =
      std::function<std::pair<RouteConfigProviderSharedPtr, const Init::Target*>(uint64_t)>;

  /**
   * @param rds RDS config.
   * @param route_config_name the name of the route config.
   * @param init_manager the init manager to register the init target with.
   * @param provider_cb a function that creates a RouteConfigProviderSharedPtr and Init::Target*.
   * The RouteConfigProviderSharedPtr is the provider that will be returned by this function.
   * The Init::Target* is the init target that will be registered with the init manager.
   * @param identifier an optional identifier for the provider. If not provided, the hash of the
   * RDS config will be used.
   */
  RouteConfigProviderSharedPtr addDynamicProvider(const Protobuf::Message& rds,
                                                  const std::string& route_config_name,
                                                  Init::Manager& init_manager,
                                                  RouteConfigProviderCb provider_cb,
                                                  absl::optional<uint64_t> identifier);

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

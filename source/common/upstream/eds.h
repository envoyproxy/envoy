#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"

#include "common/upstream/locality.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the Endpoint Discovery Service.
 */
class EdsClusterImpl : public BaseDynamicClusterImpl,
                       Config::SubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> {
public:
  EdsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Secondary; }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }

private:
  using LocalityWeightsMap =
      std::unordered_map<envoy::api::v2::core::Locality, uint32_t, LocalityHash, LocalityEqualTo>;
  bool updateHostsPerLocality(const uint32_t priority, const HostVector& new_hosts,
                              LocalityWeightsMap& locality_weights_map,
                              LocalityWeightsMap& new_locality_weights_map,
                              PriorityStateManager& priority_state_manager);

  // ClusterImplBase
  void startPreInit() override;

  const ClusterManager& cm_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>> subscription_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  std::vector<LocalityWeightsMap> locality_weights_map_;
};

} // namespace Upstream
} // namespace Envoy

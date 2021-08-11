#pragma once

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/locality.h"

#include "source/common/config/subscription_base.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the Endpoint Discovery Service.
 */
class EdsClusterImpl
    : public BaseDynamicClusterImpl,
      Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::ClusterLoadAssignment> {
public:
  EdsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return initialize_phase_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  using LocalityWeightsMap = absl::node_hash_map<envoy::config::core::v3::Locality, uint32_t,
                                                 LocalityHash, LocalityEqualTo>;
  bool updateHostsPerLocality(const uint32_t priority, const uint32_t overprovisioning_factor,
                              const HostVector& new_hosts, LocalityWeightsMap& locality_weights_map,
                              LocalityWeightsMap& new_locality_weights_map,
                              PriorityStateManager& priority_state_manager, HostMap& updated_hosts,
                              const absl::flat_hash_set<std::string>& all_new_hosts);
  bool validateUpdateSize(int num_resources);

  // ClusterImplBase
  void reloadHealthyHostsHelper(const HostSharedPtr& host) override;
  void startPreInit() override;
  void onAssignmentTimeout();

  class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
  public:
    BatchUpdateHelper(
        EdsClusterImpl& parent,
        const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment)
        : parent_(parent), cluster_load_assignment_(cluster_load_assignment) {}

    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

  private:
    EdsClusterImpl& parent_;
    const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment_;
  };

  Config::SubscriptionPtr subscription_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  std::vector<LocalityWeightsMap> locality_weights_map_;
  HostMap all_hosts_;
  Event::TimerPtr assignment_timeout_;
  InitializePhase initialize_phase_;
};

using EdsClusterImplSharedPtr = std::shared_ptr<EdsClusterImpl>;

class EdsClusterFactory : public ClusterFactoryImplBase {
public:
  EdsClusterFactory() : ClusterFactoryImplBase("envoy.cluster.eds") {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

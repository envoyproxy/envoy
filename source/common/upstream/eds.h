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

#include "common/config/subscription_base.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/leds.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

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

  // Returns true iff all the LEDS based localities are active.
  bool validateAllLedsActive() const;

  // ClusterImplBase
  void reloadHealthyHostsHelper(const HostSharedPtr& host) override;
  void startPreInit() override;
  void onAssignmentTimeout();

  class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
  public:
    BatchUpdateHelper(EdsClusterImpl& parent) : parent_(parent) {}

    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

  private:
    EdsClusterImpl& parent_;
  };

  Config::SubscriptionPtr subscription_;
  Server::Configuration::TransportSocketFactoryContextImpl factory_context_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  std::vector<LocalityWeightsMap> locality_weights_map_;
  HostMap all_hosts_;
  Event::TimerPtr assignment_timeout_;
  InitializePhase initialize_phase_;
  using LedsConfigSet = absl::flat_hash_set<envoy::config::endpoint::v3::LedsClusterLocalityConfig,
                                            LedsConfigHash, LedsConfigEqualTo>;
  using LedsConfigMap = absl::flat_hash_map<envoy::config::endpoint::v3::LedsClusterLocalityConfig,
                                            LedsSubscriptionPtr, LedsConfigHash, LedsConfigEqualTo>;
  // Maps between a LEDS configuration (ConfigSource + collection name) to the locality endpoints
  // data.
  LedsConfigMap leds_localities_;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
};

using EdsClusterImplSharedPtr = std::shared_ptr<EdsClusterImpl>;

class EdsClusterFactory : public ClusterFactoryImplBase {
public:
  EdsClusterFactory() : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().Eds) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

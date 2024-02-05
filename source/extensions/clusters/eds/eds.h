#pragma once

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/eds_resources_cache.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/locality.h"

#include "source/common/config/subscription_base.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/eds/leds.h"

namespace Envoy {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the Endpoint Discovery Service.
 */
class EdsClusterImpl
    : public BaseDynamicClusterImpl,
      Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::ClusterLoadAssignment>,
      private Config::EdsResourceRemovalCallback {
public:
  EdsClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                 ClusterFactoryContext& cluster_context);
  ~EdsClusterImpl() override;

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return initialize_phase_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  using LocalityWeightsMap = absl::node_hash_map<envoy::config::core::v3::Locality, uint32_t,
                                                 LocalityHash, LocalityEqualTo>;
  bool updateHostsPerLocality(const uint32_t priority, bool weighted_priority_health,
                              const uint32_t overprovisioning_factor, const HostVector& new_hosts,
                              LocalityWeightsMap& locality_weights_map,
                              LocalityWeightsMap& new_locality_weights_map,
                              PriorityStateManager& priority_state_manager,
                              const HostMap& all_hosts,
                              const absl::flat_hash_set<std::string>& all_new_hosts);
  bool validateUpdateSize(int num_resources);
  const std::string& edsServiceName() const {
    const std::string& name = info_->edsServiceName();
    return !name.empty() ? name : info_->name();
  }

  // Updates the internal data structures with a given cluster load assignment.
  void update(const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment);

  // EdsResourceRemovalCallback
  void onCachedResourceRemoved(absl::string_view resource_name) override;

  // ClusterImplBase
  void reloadHealthyHostsHelper(const HostSharedPtr& host) override;
  void startPreInit() override;
  void onAssignmentTimeout();

  // Returns true iff all the LEDS based localities were updated.
  bool validateAllLedsUpdated() const;

  class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
  public:
    BatchUpdateHelper(
        EdsClusterImpl& parent,
        const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment)
        : parent_(parent), cluster_load_assignment_(cluster_load_assignment) {}

    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

  private:
    void updateLocalityEndpoints(
        const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint,
        const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
        PriorityStateManager& priority_state_manager,
        absl::flat_hash_set<std::string>& all_new_hosts);

    EdsClusterImpl& parent_;
    const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment_;
  };

  Config::SubscriptionPtr subscription_;
  const LocalInfo::LocalInfo& local_info_;
  std::vector<LocalityWeightsMap> locality_weights_map_;
  Event::TimerPtr assignment_timeout_;
  InitializePhase initialize_phase_;
  using LedsConfigSet = absl::flat_hash_set<envoy::config::endpoint::v3::LedsClusterLocalityConfig,
                                            MessageUtil, MessageUtil>;
  using LedsConfigMap = absl::flat_hash_map<envoy::config::endpoint::v3::LedsClusterLocalityConfig,
                                            LedsSubscriptionPtr, MessageUtil, MessageUtil>;
  // Maps between a LEDS configuration (ConfigSource + collection name) to the locality endpoints
  // data.
  LedsConfigMap leds_localities_;
  // TODO(adisuissa): Avoid saving the entire cluster load assignment, only the
  // relevant parts of the config for each locality. Note that this field must
  // be set when LEDS is used.
  std::unique_ptr<envoy::config::endpoint::v3::ClusterLoadAssignment> cluster_load_assignment_;

  // An optional cache for the EDS resources.
  // Upon a (warming) timeout, a cached resource will be used.
  Config::EdsResourcesCacheOptRef eds_resources_cache_;

  // Tracks whether a cached resource is used as the current EDS resource.
  bool using_cached_resource_{false};
};

using EdsClusterImplSharedPtr = std::shared_ptr<EdsClusterImpl>;

class EdsClusterFactory : public ClusterFactoryImplBase {
public:
  EdsClusterFactory() : ClusterFactoryImplBase("envoy.cluster.eds") {}

private:
  absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
  createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(EdsClusterFactory);

} // namespace Upstream
} // namespace Envoy

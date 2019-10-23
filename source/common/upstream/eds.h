#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/locality.h"

#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the Endpoint Discovery Service.
 */
class EdsClusterImpl : public BaseDynamicClusterImpl, Config::SubscriptionCallbacks {
public:
  EdsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return initialize_phase_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }

  using LocalityWeightsMap =
      std::unordered_map<envoy::api::v2::core::Locality, uint32_t, LocalityHash, LocalityEqualTo>;
  bool updateHostsPerLocality(const uint32_t priority, const uint32_t overprovisioning_factor,
                              const HostVector& new_hosts, LocalityWeightsMap& locality_weights_map,
                              LocalityWeightsMap& new_locality_weights_map,
                              PriorityStateManager& priority_state_manager,
                              std::unordered_map<std::string, HostSharedPtr>& updated_hosts);
  bool validateUpdateSize(int num_resources);

  // ClusterImplBase
  void reloadHealthyHostsHelper(const HostSharedPtr& host) override;
  void startPreInit() override;
  void onAssignmentTimeout();

  class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
  public:
    BatchUpdateHelper(EdsClusterImpl& parent,
                      const envoy::api::v2::ClusterLoadAssignment& cluster_load_assignment)
        : parent_(parent), cluster_load_assignment_(cluster_load_assignment) {}

    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

  private:
    EdsClusterImpl& parent_;
    const envoy::api::v2::ClusterLoadAssignment& cluster_load_assignment_;
  };

  const ClusterManager& cm_;
  std::unique_ptr<Config::Subscription> subscription_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  std::vector<LocalityWeightsMap> locality_weights_map_;
  HostMap all_hosts_;
  Event::TimerPtr assignment_timeout_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  InitializePhase initialize_phase_;
};

class EdsClusterFactory : public ClusterFactoryImplBase {
public:
  EdsClusterFactory() : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().Eds) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

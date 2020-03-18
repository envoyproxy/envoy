#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/discovery_service_base.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
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
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::endpoint::v3::ClusterLoadAssignment>(resource)
        .cluster_name();
  }
  using LocalityWeightsMap = std::unordered_map<envoy::config::core::v3::Locality, uint32_t,
                                                LocalityHash, LocalityEqualTo>;
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
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

#pragma once

#include "envoy/config/cluster/v3alpha/cluster.pb.h"
#include "envoy/config/core/v3alpha/base.pb.h"
#include "envoy/config/core/v3alpha/config_source.pb.h"
#include "envoy/config/endpoint/v3alpha/endpoint.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/service/discovery/v3alpha/discovery.pb.h"
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
  EdsClusterImpl(const envoy::config::cluster::v3alpha::Cluster& cluster, Runtime::Loader& runtime,
                 Server::Configuration::TransportSocketFactoryContext& factory_context,
                 Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return initialize_phase_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void
  onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::service::discovery::v3alpha::Resource>&,
                 const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::endpoint::v3alpha::ClusterLoadAssignment>(
               resource)
        .cluster_name();
  }
  static std::string loadTypeUrl(envoy::config::core::v3alpha::ApiVersion resource_api_version);
  using LocalityWeightsMap = std::unordered_map<envoy::config::core::v3alpha::Locality, uint32_t,
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
        const envoy::config::endpoint::v3alpha::ClusterLoadAssignment& cluster_load_assignment)
        : parent_(parent), cluster_load_assignment_(cluster_load_assignment) {}

    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

  private:
    EdsClusterImpl& parent_;
    const envoy::config::endpoint::v3alpha::ClusterLoadAssignment& cluster_load_assignment_;
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
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
  createClusterImpl(const envoy::config::cluster::v3alpha::Cluster& cluster,
                    ClusterFactoryContext& context,
                    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                    Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy

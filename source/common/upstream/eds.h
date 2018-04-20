#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"

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
                 Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                 const LocalInfo::LocalInfo& local_info, ClusterManager& cm,
                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                 bool added_via_api);

  const std::string versionInfo() const { return subscription_->versionInfo(); }

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Secondary; }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }

private:
  using LocalityWeightsMap =
      std::unordered_map<envoy::api::v2::core::Locality, uint32_t, LocalityHash, LocalityEqualTo>;
  bool updateHostsPerLocality(HostSet& host_set, const HostVector& new_hosts,
                              LocalityWeightsMap& locality_weights_map,
                              LocalityWeightsMap& new_locality_weights_map);

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

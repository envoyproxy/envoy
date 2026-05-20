#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/server/health_checker_config.h"
#include "envoy/upstream/health_checker.h"

#include "envoy/extensions/health_checkers/multi/v3/multi.pb.h"

#include "source/common/common/callback_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {

/**
 * Proxy Host that intercepts active-health-check flags and stores them locally,
 * while delegating everything else to the real host. Each inner health checker
 * operates on its own set of proxy hosts, providing per-checker flag isolation.
 */
class HealthCheckHostProxy : public Upstream::Host {
public:
  HealthCheckHostProxy(Upstream::HostSharedPtr real_host);

  // Health flag interception — aggregated flags are stored locally.
  void healthFlagClear(HealthFlag flag) override;
  bool healthFlagGet(HealthFlag flag) const override;
  void healthFlagSet(HealthFlag flag) override;
  uint32_t healthFlagsGetAll() const override;
  void healthFlagsSetAll(uint32_t bits) override;

  // Host interface — delegated to real host.
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
  counters() const override;
  CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const override;
  CreateConnectionData createHealthCheckConnection(
      Event::Dispatcher& dispatcher,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
      const envoy::config::core::v3::Metadata* metadata) const override;
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges() const override;
  Health coarseHealth() const override;
  HealthStatus healthStatus() const override;
  void setEdsHealthStatus(HealthStatus health_status) override;
  HealthStatus edsHealthStatus() const override;
  uint32_t weight() const override;
  void weight(uint32_t new_weight) override;
  bool used() const override;
  Upstream::HostHandlePtr acquireHandle() const override;
  bool disableActiveHealthCheck() const override;
  void setDisableActiveHealthCheck(bool disable_active_health_check) override;

  // HostDescription interface — delegated to real host.
  bool canary() const override;
  void canary(bool is_canary) override;
  Upstream::MetadataConstSharedPtr metadata() const override;
  std::size_t metadataHash() const override;
  void metadata(Upstream::MetadataConstSharedPtr new_metadata) override;
  const Upstream::ClusterInfo& cluster() const override;
  bool canCreateConnection(Upstream::ResourcePriority priority) const override;
  Upstream::Outlier::DetectorHostMonitor& outlierDetector() const override;
  void setOutlierDetector(Upstream::Outlier::DetectorHostMonitorPtr&& outlier_detector) override;
  Upstream::HealthCheckHostMonitor& healthChecker() const override;
  void setHealthChecker(Upstream::HealthCheckHostMonitorPtr&& health_checker) override;
  const std::string& hostnameForHealthChecks() const override;
  const std::string& hostname() const override;
  Network::UpstreamTransportSocketFactory& transportSocketFactory() const override;
  Network::Address::InstanceConstSharedPtr address() const override;
  SharedConstAddressVector addressListOrNull() const override;
  Upstream::HostStats& stats() const override;
  Upstream::LoadMetricStats& loadMetricStats() const override;
  const envoy::config::core::v3::Locality& locality() const override;
  const Upstream::MetadataConstSharedPtr localityMetadata() const override;
  Stats::StatName localityZoneStatName() const override;
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override;
  uint32_t priority() const override;
  void priority(uint32_t) override;
  absl::optional<MonotonicTime> lastHcPassTime() const override;
  void setLastHcPassTime(MonotonicTime last_hc_pass_time) override;
  Network::UpstreamTransportSocketFactory& resolveTransportSocketFactory(
      const Network::Address::InstanceConstSharedPtr& dest_address,
      const envoy::config::core::v3::Metadata* metadata,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options = nullptr)
      const override;
  void setLbPolicyData(Upstream::HostLbPolicyDataPtr lb_policy_data) override;
  OptRef<Upstream::HostLbPolicyData> lbPolicyData() const override;

  Upstream::HostSharedPtr realHost() const { return real_host_; }

private:
  static bool isInterceptedFlag(HealthFlag flag);
  static constexpr uint32_t kInterceptedMask =
      static_cast<uint32_t>(HealthFlag::FAILED_ACTIVE_HC) |
      static_cast<uint32_t>(HealthFlag::DEGRADED_ACTIVE_HC) |
      static_cast<uint32_t>(HealthFlag::PENDING_ACTIVE_HC) |
      static_cast<uint32_t>(HealthFlag::ACTIVE_HC_TIMEOUT);

  Upstream::HostSharedPtr real_host_;
  std::atomic<uint32_t> local_flags_;
  Upstream::HealthCheckHostMonitorPtr health_checker_;
};

using HealthCheckHostProxySharedPtr = std::shared_ptr<HealthCheckHostProxy>;

/**
 * Minimal HostSet implementation for the proxy priority set.
 * Only stores hosts — the health checker doesn't use the filtered/locality views.
 */
class ProxyHostSet : public Upstream::HostSet {
public:
  ProxyHostSet(uint32_t priority) : priority_(priority) {}

  const Upstream::HostVector& hosts() const override { return hosts_; }
  Upstream::HostVectorConstSharedPtr hostsPtr() const override { return hosts_ptr_; }
  const Upstream::HostVector& healthyHosts() const override { return hosts_; }
  Upstream::HealthyHostVectorConstSharedPtr healthyHostsPtr() const override {
    return healthy_hosts_ptr_;
  }
  const Upstream::HostVector& degradedHosts() const override { return empty_; }
  Upstream::DegradedHostVectorConstSharedPtr degradedHostsPtr() const override {
    return degraded_hosts_ptr_;
  }
  const Upstream::HostVector& excludedHosts() const override { return empty_; }
  Upstream::ExcludedHostVectorConstSharedPtr excludedHostsPtr() const override {
    return excluded_hosts_ptr_;
  }
  const Upstream::HostsPerLocality& hostsPerLocality() const override { return *empty_locality_; }
  Upstream::HostsPerLocalityConstSharedPtr hostsPerLocalityPtr() const override {
    return empty_locality_;
  }
  const Upstream::HostsPerLocality& healthyHostsPerLocality() const override {
    return *empty_locality_;
  }
  Upstream::HostsPerLocalityConstSharedPtr healthyHostsPerLocalityPtr() const override {
    return empty_locality_;
  }
  const Upstream::HostsPerLocality& degradedHostsPerLocality() const override {
    return *empty_locality_;
  }
  Upstream::HostsPerLocalityConstSharedPtr degradedHostsPerLocalityPtr() const override {
    return empty_locality_;
  }
  const Upstream::HostsPerLocality& excludedHostsPerLocality() const override {
    return *empty_locality_;
  }
  Upstream::HostsPerLocalityConstSharedPtr excludedHostsPerLocalityPtr() const override {
    return empty_locality_;
  }
  Upstream::LocalityWeightsConstSharedPtr localityWeights() const override { return nullptr; }
  uint32_t priority() const override { return priority_; }
  uint32_t overprovisioningFactor() const override { return 140; }
  bool weightedPriorityHealth() const override { return false; }

  void setHosts(Upstream::HostVector hosts);

private:
  uint32_t priority_;
  Upstream::HostVector hosts_;
  Upstream::HostVectorConstSharedPtr hosts_ptr_{std::make_shared<Upstream::HostVector>()};
  Upstream::HealthyHostVectorConstSharedPtr healthy_hosts_ptr_{
      std::make_shared<Upstream::HealthyHostVector>()};
  Upstream::DegradedHostVectorConstSharedPtr degraded_hosts_ptr_{
      std::make_shared<Upstream::DegradedHostVector>()};
  Upstream::ExcludedHostVectorConstSharedPtr excluded_hosts_ptr_{
      std::make_shared<Upstream::ExcludedHostVector>()};
  Upstream::HostsPerLocalityConstSharedPtr empty_locality_{
      Upstream::HostsPerLocalityImpl::empty()};
  Upstream::HostVector empty_;
};

/**
 * Minimal PrioritySet implementation that provides member update callbacks
 * and host set access for inner health checkers.
 */
class ProxyPrioritySet : public Upstream::PrioritySet {
public:
  ABSL_MUST_USE_RESULT ::Envoy::Common::CallbackHandlePtr
  addMemberUpdateCb(MemberUpdateCb callback) const override {
    return member_update_cb_helper_.add(callback);
  }
  ABSL_MUST_USE_RESULT ::Envoy::Common::CallbackHandlePtr
  addPriorityUpdateCb(PriorityUpdateCb callback) const override {
    return priority_update_cb_helper_.add(callback);
  }
  const std::vector<Upstream::HostSetPtr>& hostSetsPerPriority() const override {
    return host_sets_;
  }
  Upstream::HostMapConstSharedPtr crossPriorityHostMap() const override { return nullptr; }
  void updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                   Upstream::LocalityWeightsConstSharedPtr locality_weights,
                   const Upstream::HostVector& hosts_added,
                   const Upstream::HostVector& hosts_removed,
                   absl::optional<bool> weighted_priority_health,
                   absl::optional<uint32_t> overprovisioning_factor,
                   Upstream::HostMapConstSharedPtr cross_priority_host_map = nullptr) override;
  void batchHostUpdate(BatchUpdateCb& callback) override;

  void updateHosts(uint32_t priority, const Upstream::HostVector& hosts_added,
                   const Upstream::HostVector& hosts_removed);

private:
  ProxyHostSet& getOrCreateHostSet(uint32_t priority);

  std::vector<Upstream::HostSetPtr> host_sets_;
  mutable ::Envoy::Common::CallbackManager<void, const Upstream::HostVector&,
                                           const Upstream::HostVector&>
      member_update_cb_helper_;
  mutable ::Envoy::Common::CallbackManager<void, uint32_t, const Upstream::HostVector&,
                                           const Upstream::HostVector&>
      priority_update_cb_helper_;
};

/**
 * Proxy Cluster that wraps the real cluster but returns a proxy PrioritySet.
 * Each inner health checker gets its own ProxyCluster so it operates on
 * isolated proxy hosts.
 */
class ProxyCluster : public Upstream::Cluster {
public:
  ProxyCluster(Upstream::Cluster& real_cluster) : real_cluster_(real_cluster) {}

  Upstream::HealthChecker* healthChecker() override { return real_cluster_.healthChecker(); }
  OptRef<const Upstream::HealthChecker> healthChecker() const override {
    return static_cast<const Upstream::Cluster&>(real_cluster_).healthChecker();
  }
  Upstream::ClusterInfoConstSharedPtr info() const override { return real_cluster_.info(); }
  Upstream::Outlier::Detector* outlierDetector() override { return real_cluster_.outlierDetector(); }
  const Upstream::Outlier::Detector* outlierDetector() const override {
    return real_cluster_.outlierDetector();
  }
  void initialize(std::function<absl::Status()> callback) override {
    real_cluster_.initialize(std::move(callback));
  }
  InitializePhase initializePhase() const override { return real_cluster_.initializePhase(); }
  Upstream::PrioritySet& prioritySet() override { return proxy_priority_set_; }
  const Upstream::PrioritySet& prioritySet() const override { return proxy_priority_set_; }
  UnitFloat dropOverload() const override { return real_cluster_.dropOverload(); }
  const std::string& dropCategory() const override { return real_cluster_.dropCategory(); }
  void setDropOverload(UnitFloat drop_overload) override {
    real_cluster_.setDropOverload(drop_overload);
  }
  void setDropCategory(absl::string_view drop_category) override {
    real_cluster_.setDropCategory(drop_category);
  }

  ProxyPrioritySet& proxyPrioritySet() { return proxy_priority_set_; }

private:
  Upstream::Cluster& real_cluster_;
  ProxyPrioritySet proxy_priority_set_;
};

/**
 * Per-checker health information for a given host, used for admin observability.
 */
struct PerCheckerStatus {
  std::string stat_prefix;
  bool failed{false};
  bool degraded{false};
  bool pending{false};
};

/**
 * Wraps multiple HealthChecker instances and aggregates their results.
 * All health checkers must consider a host healthy for the aggregate to be healthy.
 */
class MultiHealthChecker : public Upstream::HealthChecker {
public:
  MultiHealthChecker(Upstream::Cluster& cluster,
                     Server::Configuration::ServerFactoryContext& server_context,
                     const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>&
                         health_check_configs);

  // HealthChecker interface.
  void addHostCheckCompleteCb(HostStatusCb callback) override;
  void start() override;

  uint32_t numCheckers() const { return checker_data_.size(); }

  std::vector<PerCheckerStatus> perCheckerStatus(const Upstream::Host& host) const;

private:
  struct PerCheckerData {
    Upstream::HealthCheckerSharedPtr checker;
    std::string stat_prefix;
    std::unique_ptr<ProxyCluster> proxy_cluster;
    absl::node_hash_map<const Upstream::Host*, HealthCheckHostProxySharedPtr> proxy_hosts;
  };

  struct PerHostState {
    uint32_t fail_bits{0};
    uint32_t degraded_bits{0};
    uint32_t pending_bits{0};
  };

  void onCheckerResult(uint32_t checker_index, const Upstream::HostSharedPtr& host,
                       Upstream::HealthTransition changed_state, Upstream::HealthState result);
  void onClusterMemberUpdate(const Upstream::HostVector& hosts_added,
                             const Upstream::HostVector& hosts_removed);
  void populateProxyHosts(const Upstream::HostVector& hosts);
  void removeProxyHosts(const Upstream::HostVector& hosts);

  Upstream::Cluster& cluster_;
  std::vector<PerCheckerData> checker_data_;
  absl::node_hash_map<const Upstream::Host*, PerHostState> host_states_;
  std::list<HostStatusCb> callbacks_;
  ::Envoy::Common::CallbackHandlePtr member_update_cb_;
};

class MultiHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.multi"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::health_checkers::multi::v3::Multi()};
  }
};

DECLARE_FACTORY(MultiHealthCheckerFactory);

} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy

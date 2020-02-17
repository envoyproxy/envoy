#pragma once

#include <set>
#include <string>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/upstream/endpoint_group_monitor.h"
#include "common/upstream/upstream_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

class EdsClusterImpl;

class EgdsClusterMapper : Logger::Loggable<Logger::Id::upstream> {
public:
  class Delegate {
  public:
    virtual ~Delegate() {}

    virtual void initializeCluster(
        const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) PURE;

    virtual void updateHosts(uint32_t priority, const HostVector& hosts_added,
                             const HostVector& hosts_removed,
                             PriorityStateManager& priority_state_manager,
                             LocalityWeightsMap& new_locality_weights_map,
                             absl::optional<uint32_t> overprovisioning_factor = absl::nullopt) PURE;
  };

  EgdsClusterMapper(
      EndpointGroupMonitorManager& monitor_manager, Delegate& delegate,
      BaseDynamicClusterImpl& cluster,
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment,
      const LocalInfo::LocalInfo& local_info);

  bool resourceExists(absl::string_view name) const;
  void addResource(absl::string_view name);
  void removeResource(absl::string_view name);
  std::set<std::string> egds_resource_names() const;

private:
  struct ActiveEndpointGroupMonitor
      : public EndpointGroupMonitor,
        public std::enable_shared_from_this<ActiveEndpointGroupMonitor> {
    ActiveEndpointGroupMonitor(EgdsClusterMapper& parent) : parent_(parent) {}
    ~ActiveEndpointGroupMonitor() = default;

    // Upstream::EndpointGroupMonitor
    void update(const envoy::config::endpoint::v3::EndpointGroup& group,
                absl::string_view version_info) override;
    void batchUpdate(const envoy::config::endpoint::v3::EndpointGroup& group,
                     absl::string_view version_info, bool all_endpoint_groups_updated) override;

    void doBatchUpdate(PrioritySet::HostUpdateCb& host_update_cb);

    void initializeEndpointGroup();
    bool
    calculateUpdatedHostsPerLocality(const uint32_t priority, const HostVector& new_hosts,
                                     std::unordered_map<std::string, HostSharedPtr>& updated_hosts,
                                     PriorityStateManager& priority_state_manager,
                                     LocalityWeightsMap& new_locality_weights_map,
                                     absl::optional<uint32_t> overprovisioning_factor);

    EgdsClusterMapper& parent_;
    absl::optional<envoy::config::endpoint::v3::EndpointGroup> group_;
    absl::optional<std::string> version_;
    PrioritySetImpl priority_set_;
    HostMap all_hosts_;
  };
  using ActiveEndpointGroupMonitorSharedPtr = std::shared_ptr<ActiveEndpointGroupMonitor>;

  class EmptyBatchUpdateScope : public PrioritySet::HostUpdateCb {
  public:
    void updateHosts(uint32_t, PrioritySet::UpdateHostsParams&&, LocalityWeightsConstSharedPtr,
                     const HostVector&, const HostVector&, absl::optional<uint32_t>) override {}
  };

  class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
  public:
    // Upstream::PrioritySet::BatchUpdateCb
    void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

    void addUpdatedMonitor(ActiveEndpointGroupMonitorSharedPtr monitor) {
      updated_monitor_.push_back(monitor);
    }

    void clear() { updated_monitor_.clear(); }

  private:
    std::deque<ActiveEndpointGroupMonitorSharedPtr> updated_monitor_;
  };

  bool clusterDataIsReady();
  void initializeCluster();
  void batchHostUpdate();
  void addUpdatedActiveMonitor(ActiveEndpointGroupMonitorSharedPtr monitor);

  absl::flat_hash_map<std::string, ActiveEndpointGroupMonitorSharedPtr> active_monitors_;
  EndpointGroupMonitorManager& monitor_manager_;
  Delegate& delegate_;
  BaseDynamicClusterImpl& cluster_;
  bool cluster_initialized_{false};
  envoy::config::endpoint::v3::ClusterLoadAssignment origin_cluster_load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
  BatchUpdateHelper batch_update_helper_;
};

using EgdsClusterMapperPtr = std::unique_ptr<EgdsClusterMapper>;

} // namespace Upstream
} // namespace Envoy

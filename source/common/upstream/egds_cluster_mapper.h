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

    virtual void batchHostUpdateForEndpointGroup(PrioritySet::BatchUpdateCb& callback) PURE;
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
  struct ActiveEndpointGroupMonitor : public EndpointGroupMonitor {
    ActiveEndpointGroupMonitor(EgdsClusterMapper& parent) : parent_(parent) {}
    ~ActiveEndpointGroupMonitor() = default;

    // Upstream::EndpointGroupMonitor
    void update(const envoy::config::endpoint::v3::EndpointGroup& group,
                absl::string_view version_info);

    void initializeEndpointGroup();

    class BatchUpdateHelper : public PrioritySet::BatchUpdateCb {
    public:
      BatchUpdateHelper(ActiveEndpointGroupMonitor& parent, bool notify_hosts_updated = true)
          : parent_(parent), notify_hosts_updated_(notify_hosts_updated) {}

      // Upstream::PrioritySet::BatchUpdateCb
      void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override;

      bool calculateUpdatedHostsPerLocality(
          const uint32_t priority, const HostVector& new_hosts,
          std::unordered_map<std::string, HostSharedPtr>& updated_hosts,
          PriorityStateManager& priority_state_manager,
          LocalityWeightsMap& new_locality_weights_map,
          absl::optional<uint32_t> overprovisioning_factor);

    private:
      ActiveEndpointGroupMonitor& parent_;
      const bool notify_hosts_updated_;
    };

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

  bool clusterDataIsReady();
  void initializeCluster();

  absl::flat_hash_map<std::string, ActiveEndpointGroupMonitorSharedPtr> active_monitors_;
  EndpointGroupMonitorManager& monitor_manager_;
  Delegate& delegate_;
  BaseDynamicClusterImpl& cluster_;
  bool cluster_initialized_{false};
  envoy::config::endpoint::v3::ClusterLoadAssignment origin_cluster_load_assignment_;
  const LocalInfo::LocalInfo& local_info_;
};

using EgdsClusterMapperPtr = std::unique_ptr<EgdsClusterMapper>;

} // namespace Upstream
} // namespace Envoy

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/schedulable_cb.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class ClusterWriterSharedState;

/**
 * Adds, updates and removes the clusters of a bootstrap dynamic module. Writes are queued and
 * applied on the main thread in later event loop iterations, and the outcome of every accepted
 * write is reported through the done callback. All methods must be called on the main thread.
 */
class DynamicModuleClusterWriter : public Upstream::ClusterUpdateCallbacks,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // Reports the outcome of a write. The string views are valid only during the call.
  using UpdateDoneCallback =
      std::function<void(uint64_t update_id, absl::string_view cluster_name,
                         envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
                         absl::string_view error_details)>;

  /**
   * @param extension_name the name of the bootstrap extension.
   * @param context the server factory context.
   * @param update_done the callback that reports outcomes, or nullptr when the module has no done
   * hook.
   */
  DynamicModuleClusterWriter(absl::string_view extension_name,
                             Server::Configuration::ServerFactoryContext& context,
                             UpdateDoneCallback update_done);

  /**
   * Called once the server is initialized. Writes are not allowed before this.
   */
  void onServerInitialized();

  /**
   * Called when shutdown begins. Refuses later writes and drops the queued work.
   */
  void onShutdown();

  /**
   * Queues a write of the given serialized cluster.
   * @param update_id is set to the ID of the write, or to 0 when the write is rejected.
   */
  envoy_dynamic_module_type_bootstrap_cluster_writer_update_result
  addOrUpdateCluster(absl::string_view cluster_name, absl::string_view cluster,
                     absl::string_view version_info, uint64_t& update_id);

  /**
   * Queues the removal of the given cluster.
   */
  envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result
  removeCluster(absl::string_view cluster_name);

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(absl::string_view cluster_name) override;

private:
  // A queued write or removal of a cluster.
  struct QueuedOperation {
    std::string cluster_name;
    // The sequence number of the latest submission for the cluster.
    uint64_t sequence;
    // The ID of the write, or 0 for a removal.
    uint64_t update_id;
    std::string cluster;
    std::string version_info;
  };

  // What the writer knows about a cluster that it owns or has queued an operation for.
  struct ClusterState {
    explicit ClusterState(std::list<QueuedOperation>::iterator queue_end) : queued(queue_end) {}

    bool owned = false;
    // The queued operation of the cluster, or the end of the queue when none is queued.
    std::list<QueuedOperation>::iterator queued;
    // The write applied to the cluster manager that waits for the cluster to finish warming, or 0.
    uint64_t warming_update_id = 0;
    // The cluster manager keeps the cluster of a failed write active, but the workers do not use
    // it.
    bool failed_write_kept = false;
  };

  // The outcome of a write that is not reported to the module yet.
  struct UpdateOutcome {
    uint64_t update_id;
    std::string cluster_name;
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status;
    std::string error_details;
  };

  struct WriterStats {
    Stats::Counter& update_submitted_;
    Stats::Counter& update_rejected_;
    Stats::Counter& update_active_;
    Stats::Counter& update_superseded_;
    Stats::Counter& update_removed_;
    Stats::Counter& update_failed_;
    Stats::Counter& update_ownership_lost_;
    Stats::Counter& ownership_lost_;
    Stats::Gauge& clusters_owned_;
    Stats::Gauge& updates_pending_;
  };

  static WriterStats generateStats(Stats::Scope& scope, absl::string_view config_name);

  void onClustersInitialized();
  bool writesAllowed() const;
  void scheduleApplyPass();
  void applyQueuedOperations();
  void applyUpdate(QueuedOperation& operation);
  void applyRemoval(const QueuedOperation& operation);
  absl::Status validateUpdate(const QueuedOperation& operation, const ClusterState& state,
                              envoy::config::cluster::v3::Cluster& cluster) const;
  void setOwned(ClusterState& state, bool owned);
  void loseOwnership(absl::string_view cluster_name, ClusterState& state,
                     absl::string_view error_details);
  void completeUpdate(uint64_t update_id, absl::string_view cluster_name,
                      envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
                      absl::string_view error_details = {});
  void
  completeWarmingUpdate(absl::string_view cluster_name, ClusterState& state,
                        envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
                        absl::string_view error_details = {});
  void deliverOutcomes();
  void eraseIfUnused(absl::string_view cluster_name);

  // The extension name, or "default" when it is empty.
  const std::string config_name_;
  Server::Configuration::ServerFactoryContext& context_;
  const std::shared_ptr<ClusterWriterSharedState> shared_state_;
  const UpdateDoneCallback update_done_;
  // Owned, since the server scope goes away before the bootstrap extensions at shutdown.
  const Stats::ScopeSharedPtr stats_scope_;
  WriterStats stats_;

  // The queued operations in the order of their first submission.
  std::list<QueuedOperation> queue_;
  // Node based so that references stay valid while module hooks queue more operations.
  absl::node_hash_map<std::string, ClusterState> clusters_;
  std::vector<UpdateOutcome> undelivered_outcomes_;

  uint64_t next_update_id_ = 1;
  uint64_t next_sequence_ = 0;
  bool server_initialized_ = false;
  bool shut_down_ = false;
  // Set once Envoy finished initializing its static and CDS clusters.
  bool clusters_initialized_ = false;
  // The cluster being applied, and whether it became active inside the cluster manager call.
  absl::string_view applying_cluster_name_;
  bool applying_cluster_activated_ = false;

  Init::TargetImpl init_target_;
  Event::SchedulableCallbackPtr apply_callback_;
  Event::SchedulableCallbackPtr deliver_callback_;
  Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_handle_;
};

using DynamicModuleClusterWriterPtr = std::unique_ptr<DynamicModuleClusterWriter>;

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

#pragma once

#include <cstdint>
#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/bootstrap/dynamic_modules/resource_writer.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

// What the cluster writer knows about a cluster that it owns or has queued an operation for.
struct ClusterWriterState : public WriterResourceState {
  using WriterResourceState::WriterResourceState;

  // The cluster manager keeps the cluster of a failed write active, but the workers do not use it.
  bool failed_write_kept = false;
};

/**
 * Adds, updates and removes the clusters of a bootstrap dynamic module. Writes are queued and
 * applied on the main thread in later event loop iterations, and the outcome of every accepted
 * write is reported through the done callback. All methods must be called on the main thread.
 */
class DynamicModuleClusterWriter
    : public ResourceWriter<ClusterWriterState,
                            envoy_dynamic_module_type_bootstrap_cluster_writer_update_status>,
      public Upstream::ClusterUpdateCallbacks {
public:
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
  // ResourceWriter
  void applyUpdate(WriterOperation& operation) override;
  void applyRemoval(const WriterOperation& operation) override;

  void onClustersInitialized();
  absl::Status validateUpdate(const WriterOperation& operation, const ClusterWriterState& state,
                              envoy::config::cluster::v3::Cluster& cluster) const;
  void loseOwnership(absl::string_view cluster_name, ClusterWriterState& state,
                     absl::string_view error_details);

  // Whether the cluster being applied became active inside the cluster manager call.
  bool applying_cluster_activated_ = false;
  Init::TargetImpl init_target_;
  Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_handle_;
};

using DynamicModuleClusterWriterPtr = std::unique_ptr<DynamicModuleClusterWriter>;

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

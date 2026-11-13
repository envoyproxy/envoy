#include "source/extensions/bootstrap/dynamic_modules/cluster_writer.h"

#include <chrono>
#include <utility>

#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/init/manager.h"

#include "source/common/common/fmt.h"
#include "source/common/config/resource_name.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

static_assert(static_cast<int>(WriteOutcome::Active) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Active);
static_assert(static_cast<int>(WriteOutcome::Superseded) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded);
static_assert(static_cast<int>(WriteOutcome::Removed) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Removed);
static_assert(static_cast<int>(WriteOutcome::Failed) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed);
static_assert(static_cast<int>(WriteOutcome::OwnershipLost) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_OwnershipLost);
static_assert(static_cast<int>(RemovalResult::Success) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success);
static_assert(static_cast<int>(RemovalResult::NotAllowed) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotAllowed);
static_assert(static_cast<int>(RemovalResult::NotFound) ==
              envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound);

DynamicModuleClusterWriter::DynamicModuleClusterWriter(
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateDoneCallback update_done)
    : ResourceWriter(extension_name, context, std::move(update_done),
                     {"cluster_writer", "clusters_owned"},
                     Config::getTypeUrl<envoy::config::cluster::v3::Cluster>()),
      init_target_(absl::StrCat("dynamic_modules_bootstrap_cluster_writer:", config_name_),
                   [this]() {
                     onClustersInitialized();
                     init_target_.ready();
                   }) {}

void DynamicModuleClusterWriter::onServerInitialized() {
  server_initialized_ = true;
  // The server init manager starts once Envoy initialized its static and CDS clusters. A write
  // applied before that could finish the cluster manager initialization early.
  if (context_.initManager().state() == Init::Manager::State::Uninitialized) {
    context_.initManager().add(init_target_);
  } else {
    onClustersInitialized();
  }
}

void DynamicModuleClusterWriter::onClustersInitialized() {
  // Only a running server starts its init manager, and it calls onShutdown() before it frees the
  // cluster manager.
  if (!shut_down_) {
    cluster_update_callbacks_handle_ =
        context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
  }
  startApplying();
}

void DynamicModuleClusterWriter::onShutdown() {
  // The thread local cluster manager goes away before this writer does.
  cluster_update_callbacks_handle_.reset();
  shutDown();
}

envoy_dynamic_module_type_bootstrap_cluster_writer_update_result
DynamicModuleClusterWriter::addOrUpdateCluster(absl::string_view cluster_name,
                                               absl::string_view cluster,
                                               absl::string_view version_info,
                                               uint64_t& update_id) {
  return queueWrite(cluster_name, cluster, version_info, update_id)
             ? envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_Success
             : envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_NotAllowed;
}

envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result
DynamicModuleClusterWriter::removeCluster(absl::string_view cluster_name) {
  return static_cast<envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result>(
      queueRemoval(cluster_name));
}

absl::Status
DynamicModuleClusterWriter::validateUpdate(const WriterOperation& operation,
                                           const ClusterWriterState& state,
                                           envoy::config::cluster::v3::Cluster& cluster) const {
  RETURN_IF_NOT_OK(parseAndValidate(operation, "cluster", cluster));
  if (!state.owned && context_.clusterManager().hasCluster(operation.name)) {
    return absl::AlreadyExistsError(
        fmt::format("cluster {} exists and is not owned by this extension", operation.name));
  }
  return absl::OkStatus();
}

void DynamicModuleClusterWriter::applyUpdate(WriterOperation& operation) {
  ClusterWriterState& state = states_.find(operation.name)->second;
  envoy::config::cluster::v3::Cluster cluster;
  if (absl::Status status = validateUpdate(operation, state, cluster); !status.ok()) {
    ENVOY_LOG_PERIODIC(
        warn, std::chrono::seconds(10),
        "dynamic module cluster writer {} rejected update {} of cluster {} at version {}: {}",
        config_name_, operation.update_id, operation.name, operation.version_info,
        status.message());
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed, status.message());
    eraseIfUnused(operation.name);
    return;
  }

  // An activation inside the cluster manager call belongs to this write.
  const uint64_t previous_warming_update_id = std::exchange(state.warming_update_id, 0);
  const Upstream::Cluster* active_cluster =
      context_.clusterManager().getActiveCluster(operation.name).ptr();
  applying_cluster_activated_ = false;
  const absl::StatusOr<bool> update_or_error = applyWithManager(operation.name, [&]() {
    return context_.clusterManager().addOrUpdateCluster(cluster, operation.version_info,
                                                        /*avoid_cds_removal=*/true);
  });

  if (!update_or_error.ok()) {
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(10),
                       "dynamic module cluster writer {} failed to apply update {} of cluster {} "
                       "at version {}: {}",
                       config_name_, operation.update_id, operation.name, operation.version_info,
                       update_or_error.status().message());
    // The cluster manager keeps the new cluster when its load balancer failed to initialize.
    setOwned(state, state.owned || context_.clusterManager().hasCluster(operation.name));
    state.warming_update_id = previous_warming_update_id;
    if (context_.clusterManager().getActiveCluster(operation.name).ptr() != active_cluster) {
      // The new cluster became active before the failure, so the earlier write stopped warming.
      completeWarmingUpdate(operation.name, state, WriteOutcome::Superseded);
      state.failed_write_kept = true;
    }
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed,
                   fmt::format("the cluster manager rejected cluster {}", operation.name));
    eraseIfUnused(operation.name);
    return;
  }

  setOwned(state, true);
  // A change or an activation replaces the cluster of an earlier failed write.
  if (update_or_error.value() || applying_cluster_activated_) {
    state.failed_write_kept = false;
  }
  // This write replaces the warming one, even when the cluster manager found them identical.
  if (previous_warming_update_id != 0) {
    completeUpdate(previous_warming_update_id, operation.name, WriteOutcome::Superseded);
  }
  // An unchanged write with nothing warming matches the active configuration.
  if (applying_cluster_activated_ ||
      (!update_or_error.value() && previous_warming_update_id == 0)) {
    if (state.failed_write_kept) {
      completeUpdate(
          operation.update_id, operation.name, WriteOutcome::Failed,
          fmt::format("the cluster manager failed to initialize cluster {}", operation.name));
    } else {
      completeUpdate(operation.update_id, operation.name, WriteOutcome::Active);
    }
  } else {
    state.warming_update_id = operation.update_id;
  }
}

void DynamicModuleClusterWriter::applyRemoval(const WriterOperation& operation) {
  ClusterWriterState& state = states_.find(operation.name)->second;
  if (state.owned) {
    // Give up ownership first so that the removal does not look like a takeover.
    setOwned(state, false);
    completeWarmingUpdate(operation.name, state, WriteOutcome::Removed);
    context_.clusterManager().removeCluster(operation.name, /*remove_ignored=*/true);
  }
  eraseIfUnused(operation.name);
}

void DynamicModuleClusterWriter::onClusterAddOrUpdate(absl::string_view cluster_name,
                                                      Upstream::ThreadLocalClusterCommand&) {
  if (cluster_name == applyingName()) {
    applying_cluster_activated_ = true;
    return;
  }
  auto it = states_.find(cluster_name);
  if (it == states_.end() || !it->second.owned) {
    return;
  }
  if (it->second.warming_update_id != 0) {
    completeWarmingUpdate(cluster_name, it->second, WriteOutcome::Active);
    return;
  }
  loseOwnership(cluster_name, it->second,
                fmt::format("cluster {} was replaced by another component", cluster_name));
}

void DynamicModuleClusterWriter::onClusterRemoval(absl::string_view cluster_name) {
  auto it = states_.find(cluster_name);
  if (it == states_.end() || !it->second.owned) {
    return;
  }
  loseOwnership(cluster_name, it->second,
                fmt::format("cluster {} was removed by another component", cluster_name));
}

void DynamicModuleClusterWriter::loseOwnership(absl::string_view cluster_name,
                                               ClusterWriterState& state,
                                               absl::string_view error_details) {
  ENVOY_LOG(warn, "dynamic module cluster writer {} no longer owns cluster {}: {}", config_name_,
            cluster_name, error_details);
  stats_.ownership_lost_.inc();
  completeWarmingUpdate(cluster_name, state, WriteOutcome::OwnershipLost, error_details);
  setOwned(state, false);
  eraseIfUnused(cluster_name);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

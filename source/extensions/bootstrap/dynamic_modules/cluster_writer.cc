#include "source/extensions/bootstrap/dynamic_modules/cluster_writer.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/xds_manager.h"
#include "envoy/init/manager.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/fmt.h"
#include "source/common/config/dependent_type_urls.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

namespace {

// How long an apply pass may run before it yields to the event loop.
constexpr std::chrono::milliseconds ApplyPassBudget{5};
// How long the passes of a burst may hold back the requests of the dependent xDS types.
constexpr std::chrono::milliseconds MaxDependentTypePause{1000};
constexpr absl::string_view ClusterWriterStatPrefix = "cluster_writer";

} // namespace

// The state that the cluster writers of a server share. Their apply passes hold back the requests
// of the xDS types that clusters depend on with one pause, so that its time limit holds when their
// bursts overlap.
class ClusterWriterSharedState : public Singleton::Instance {
public:
  static std::shared_ptr<ClusterWriterSharedState>
  get(Server::Configuration::ServerFactoryContext& context);

  // The writers whose burst of writes is not fully applied yet.
  absl::flat_hash_set<const DynamicModuleClusterWriter*> writers_applying_;
  Config::ScopedResume dependent_type_pause_;
  MonotonicTime dependent_type_pause_start_;
};

SINGLETON_MANAGER_REGISTRATION(dynamic_modules_bootstrap_cluster_writer_shared_state);

std::shared_ptr<ClusterWriterSharedState>
ClusterWriterSharedState::get(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<ClusterWriterSharedState>(
      SINGLETON_MANAGER_REGISTERED_NAME(dynamic_modules_bootstrap_cluster_writer_shared_state),
      [] { return std::make_shared<ClusterWriterSharedState>(); });
}

DynamicModuleClusterWriter::DynamicModuleClusterWriter(
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateDoneCallback update_done)
    : config_name_(extension_name.empty() ? "default" : extension_name), context_(context),
      shared_state_(ClusterWriterSharedState::get(context)), update_done_(std::move(update_done)),
      stats_scope_(context.serverScope().store().createScope("")),
      stats_(generateStats(*stats_scope_, config_name_)),
      init_target_(absl::StrCat("dynamic_modules_bootstrap_cluster_writer:", config_name_),
                   [this]() {
                     onClustersInitialized();
                     init_target_.ready();
                   }),
      apply_callback_(context.mainThreadDispatcher().createSchedulableCallback(
          [this]() { applyQueuedOperations(); })),
      deliver_callback_(context.mainThreadDispatcher().createSchedulableCallback(
          [this]() { deliverOutcomes(); })) {}

DynamicModuleClusterWriter::WriterStats
DynamicModuleClusterWriter::generateStats(Stats::Scope& scope, absl::string_view config_name) {
  Stats::StatNameDynamicPool pool(scope.symbolTable());
  const Stats::StatNameTagVector tags{
      {pool.add("config_name"), pool.add(Stats::Utility::sanitizeStatsName(config_name))}};
  const auto elements = [](absl::string_view name) -> Stats::ElementVec {
    return {Stats::DynamicName(Extensions::DynamicModules::DynamicModulesStatRoot),
            Stats::DynamicName(ClusterWriterStatPrefix), Stats::DynamicName(name)};
  };
  const auto counter = [&](absl::string_view name) -> Stats::Counter& {
    return Stats::Utility::counterFromElements(scope, elements(name), tags);
  };
  const auto gauge = [&](absl::string_view name) -> Stats::Gauge& {
    return Stats::Utility::gaugeFromElements(scope, elements(name),
                                             Stats::Gauge::ImportMode::NeverImport, tags);
  };
  return WriterStats{counter("update_submitted"),      counter("update_rejected"),
                     counter("update_active"),         counter("update_superseded"),
                     counter("update_removed"),        counter("update_failed"),
                     counter("update_ownership_lost"), counter("ownership_lost"),
                     gauge("clusters_owned"),          gauge("updates_pending")};
}

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
  clusters_initialized_ = true;
  // Only a running server starts its init manager, and it calls onShutdown() before it frees the
  // cluster manager.
  if (!shut_down_) {
    cluster_update_callbacks_handle_ =
        context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
  }
  scheduleApplyPass();
}

void DynamicModuleClusterWriter::onShutdown() {
  shut_down_ = true;
  // The thread local cluster manager goes away before this writer does.
  cluster_update_callbacks_handle_.reset();
  apply_callback_->cancel();
  deliver_callback_->cancel();
  shared_state_->writers_applying_.erase(this);
  if (shared_state_->writers_applying_.empty()) {
    shared_state_->dependent_type_pause_.reset();
  }
  clusters_.clear();
  queue_.clear();
  undelivered_outcomes_.clear();
  stats_.clusters_owned_.set(0);
  stats_.updates_pending_.set(0);
}

bool DynamicModuleClusterWriter::writesAllowed() const {
  return server_initialized_ && !shut_down_;
}

envoy_dynamic_module_type_bootstrap_cluster_writer_update_result
DynamicModuleClusterWriter::addOrUpdateCluster(absl::string_view cluster_name,
                                               absl::string_view cluster,
                                               absl::string_view version_info,
                                               uint64_t& update_id) {
  update_id = 0;
  if (!writesAllowed()) {
    stats_.update_rejected_.inc();
    return envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_NotAllowed;
  }

  ClusterState& state = clusters_.try_emplace(cluster_name, queue_.end()).first->second;
  update_id = next_update_id_++;
  stats_.update_submitted_.inc();
  stats_.updates_pending_.inc();
  if (state.queued == queue_.end()) {
    state.queued = queue_.insert(
        queue_.end(), QueuedOperation{std::string(cluster_name), next_sequence_++, update_id,
                                      std::string(cluster), std::string(version_info)});
  } else {
    // The queued operation keeps its place in the queue and takes the latest write.
    QueuedOperation& operation = *state.queued;
    if (operation.update_id != 0) {
      completeUpdate(operation.update_id, cluster_name,
                     envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded);
    }
    operation.sequence = next_sequence_++;
    operation.update_id = update_id;
    operation.cluster = std::string(cluster);
    operation.version_info = std::string(version_info);
  }
  scheduleApplyPass();
  return envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_Success;
}

envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result
DynamicModuleClusterWriter::removeCluster(absl::string_view cluster_name) {
  if (!writesAllowed()) {
    return envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotAllowed;
  }
  auto it = clusters_.find(cluster_name);
  if (it == clusters_.end()) {
    return envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound;
  }

  ClusterState& state = it->second;
  if (state.queued == queue_.end()) {
    state.queued = queue_.insert(
        queue_.end(), QueuedOperation{std::string(cluster_name), next_sequence_++, 0, {}, {}});
  } else {
    QueuedOperation& operation = *state.queued;
    if (operation.update_id != 0) {
      completeUpdate(operation.update_id, cluster_name,
                     envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Removed);
    }
    // A cluster that is neither owned nor being applied has nothing to remove, so cancel the
    // queued write.
    if (!state.owned && cluster_name != applying_cluster_name_) {
      queue_.erase(state.queued);
      state.queued = queue_.end();
      eraseIfUnused(cluster_name);
      return envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success;
    }
    operation.sequence = next_sequence_++;
    operation.update_id = 0;
    operation.cluster = std::string();
    operation.version_info = std::string();
  }
  scheduleApplyPass();
  return envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success;
}

void DynamicModuleClusterWriter::scheduleApplyPass() {
  // Passes run in separate event loop iterations, so a module that writes again from its done hook
  // cannot keep the loop from polling.
  if (clusters_initialized_ && !queue_.empty() && !apply_callback_->enabled()) {
    apply_callback_->scheduleCallbackNextIteration();
  }
}

void DynamicModuleClusterWriter::applyQueuedOperations() {
  TimeSource& time_source = context_.mainThreadDispatcher().timeSource();
  // Send the EDS, LEDS and SDS requests of a burst of written clusters together, as CDS does.
  if (shared_state_->dependent_type_pause_ == nullptr) {
    shared_state_->dependent_type_pause_ = context_.xdsManager().pause(
        Config::dependentTypeUrls(Config::getTypeUrl<envoy::config::cluster::v3::Cluster>()));
    shared_state_->dependent_type_pause_start_ = time_source.monotonicTime();
  }
  // Operations queued while this pass runs wait for the next pass, so that every pass ends.
  const uint64_t sequence_cutoff = next_sequence_;
  const MonotonicTime deadline = time_source.monotonicTime() + ApplyPassBudget;
  bool applied_any = false;
  while (true) {
    auto it = std::find_if(queue_.begin(), queue_.end(), [sequence_cutoff](const auto& operation) {
      return operation.sequence < sequence_cutoff;
    });
    if (it == queue_.end() || (applied_any && time_source.monotonicTime() >= deadline)) {
      break;
    }
    QueuedOperation operation = std::move(*it);
    queue_.erase(it);
    clusters_.find(operation.cluster_name)->second.queued = queue_.end();
    if (operation.update_id == 0) {
      applyRemoval(operation);
    } else {
      applyUpdate(operation);
    }
    applied_any = true;
  }
  if (queue_.empty()) {
    shared_state_->writers_applying_.erase(this);
  } else {
    shared_state_->writers_applying_.insert(this);
  }
  // Modules that write all the time must not hold back these requests for long.
  if (shared_state_->writers_applying_.empty() ||
      time_source.monotonicTime() - shared_state_->dependent_type_pause_start_ >=
          MaxDependentTypePause) {
    shared_state_->dependent_type_pause_.reset();
  }
  scheduleApplyPass();
}

absl::Status
DynamicModuleClusterWriter::validateUpdate(const QueuedOperation& operation,
                                           const ClusterState& state,
                                           envoy::config::cluster::v3::Cluster& cluster) const {
  if (!cluster.ParseFromString(operation.cluster)) {
    return absl::InvalidArgumentError(
        fmt::format("failed to parse cluster {}", operation.cluster_name));
  }
  if (cluster.name() != operation.cluster_name) {
    return absl::InvalidArgumentError(fmt::format("cluster {} is named {} in its configuration",
                                                  operation.cluster_name, cluster.name()));
  }
  // Validate like CDS does, but keep the contents of the cluster out of the error.
  ProtobufMessage::ValidationVisitor& validation_visitor =
      context_.messageValidationContext().dynamicValidationVisitor();
  TRY_ASSERT_MAIN_THREAD {
    if (!validation_visitor.skipValidation()) {
      MessageUtil::checkForUnexpectedFields(cluster, validation_visitor);
    }
    MessageUtil::validateDurationFields(cluster);
  }
  END_TRY
  CATCH(const EnvoyException& e, {
    return absl::InvalidArgumentError(
        fmt::format("invalid cluster {}: {}", operation.cluster_name, e.what()));
  });
  std::string error;
  if (!Validate(cluster, &error)) {
    return absl::InvalidArgumentError(
        fmt::format("invalid cluster {}: {}", operation.cluster_name, error));
  }
  if (!state.owned && context_.clusterManager().hasCluster(operation.cluster_name)) {
    return absl::AlreadyExistsError(fmt::format(
        "cluster {} exists and is not owned by this extension", operation.cluster_name));
  }
  return absl::OkStatus();
}

void DynamicModuleClusterWriter::applyUpdate(QueuedOperation& operation) {
  ClusterState& state = clusters_.find(operation.cluster_name)->second;
  envoy::config::cluster::v3::Cluster cluster;
  if (absl::Status status = validateUpdate(operation, state, cluster); !status.ok()) {
    ENVOY_LOG_PERIODIC(
        warn, std::chrono::seconds(10),
        "dynamic module cluster writer {} rejected update {} of cluster {} at version {}: {}",
        config_name_, operation.update_id, operation.cluster_name, operation.version_info,
        status.message());
    completeUpdate(operation.update_id, operation.cluster_name,
                   envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed,
                   status.message());
    eraseIfUnused(operation.cluster_name);
    return;
  }

  // An activation inside the cluster manager call belongs to this write.
  const uint64_t previous_warming_update_id = std::exchange(state.warming_update_id, 0);
  const Upstream::Cluster* active_cluster =
      context_.clusterManager().getActiveCluster(operation.cluster_name).ptr();
  applying_cluster_name_ = operation.cluster_name;
  applying_cluster_activated_ = false;
  absl::StatusOr<bool> update_or_error = absl::UnknownError("not applied");
  TRY_ASSERT_MAIN_THREAD {
    update_or_error = context_.clusterManager().addOrUpdateCluster(cluster, operation.version_info,
                                                                   /*avoid_cds_removal=*/true);
  }
  END_TRY
  CATCH(const EnvoyException& e, { update_or_error = absl::InvalidArgumentError(e.what()); });
  applying_cluster_name_ = {};

  if (!update_or_error.ok()) {
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(10),
                       "dynamic module cluster writer {} failed to apply update {} of cluster {} "
                       "at version {}: {}",
                       config_name_, operation.update_id, operation.cluster_name,
                       operation.version_info, update_or_error.status().message());
    // The cluster manager keeps the new cluster when its load balancer failed to initialize.
    setOwned(state, state.owned || context_.clusterManager().hasCluster(operation.cluster_name));
    state.warming_update_id = previous_warming_update_id;
    if (context_.clusterManager().getActiveCluster(operation.cluster_name).ptr() !=
        active_cluster) {
      // The new cluster became active before the failure, so the earlier write stopped warming.
      completeWarmingUpdate(
          operation.cluster_name, state,
          envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded);
      state.failed_write_kept = true;
    }
    completeUpdate(operation.update_id, operation.cluster_name,
                   envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed,
                   fmt::format("the cluster manager rejected cluster {}", operation.cluster_name));
    eraseIfUnused(operation.cluster_name);
    return;
  }

  setOwned(state, true);
  // A change or an activation replaces the cluster of an earlier failed write.
  if (update_or_error.value() || applying_cluster_activated_) {
    state.failed_write_kept = false;
  }
  // This write replaces the warming one, even when the cluster manager found them identical.
  if (previous_warming_update_id != 0) {
    completeUpdate(previous_warming_update_id, operation.cluster_name,
                   envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded);
  }
  // An unchanged write with nothing warming matches the active configuration.
  if (applying_cluster_activated_ ||
      (!update_or_error.value() && previous_warming_update_id == 0)) {
    if (state.failed_write_kept) {
      completeUpdate(operation.update_id, operation.cluster_name,
                     envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed,
                     fmt::format("the cluster manager failed to initialize cluster {}",
                                 operation.cluster_name));
    } else {
      completeUpdate(operation.update_id, operation.cluster_name,
                     envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Active);
    }
  } else {
    state.warming_update_id = operation.update_id;
  }
}

void DynamicModuleClusterWriter::applyRemoval(const QueuedOperation& operation) {
  ClusterState& state = clusters_.find(operation.cluster_name)->second;
  if (state.owned) {
    // Give up ownership first so that the removal does not look like a takeover.
    setOwned(state, false);
    completeWarmingUpdate(operation.cluster_name, state,
                          envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Removed);
    context_.clusterManager().removeCluster(operation.cluster_name, /*remove_ignored=*/true);
  }
  eraseIfUnused(operation.cluster_name);
}

void DynamicModuleClusterWriter::onClusterAddOrUpdate(absl::string_view cluster_name,
                                                      Upstream::ThreadLocalClusterCommand&) {
  if (cluster_name == applying_cluster_name_) {
    applying_cluster_activated_ = true;
    return;
  }
  auto it = clusters_.find(cluster_name);
  if (it == clusters_.end() || !it->second.owned) {
    return;
  }
  if (it->second.warming_update_id != 0) {
    completeWarmingUpdate(cluster_name, it->second,
                          envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Active);
    return;
  }
  loseOwnership(cluster_name, it->second,
                fmt::format("cluster {} was replaced by another component", cluster_name));
}

void DynamicModuleClusterWriter::onClusterRemoval(absl::string_view cluster_name) {
  auto it = clusters_.find(cluster_name);
  if (it == clusters_.end() || !it->second.owned) {
    return;
  }
  loseOwnership(cluster_name, it->second,
                fmt::format("cluster {} was removed by another component", cluster_name));
}

void DynamicModuleClusterWriter::setOwned(ClusterState& state, bool owned) {
  if (state.owned == owned) {
    return;
  }
  state.owned = owned;
  if (owned) {
    stats_.clusters_owned_.inc();
  } else {
    stats_.clusters_owned_.dec();
  }
}

void DynamicModuleClusterWriter::loseOwnership(absl::string_view cluster_name, ClusterState& state,
                                               absl::string_view error_details) {
  ENVOY_LOG(warn, "dynamic module cluster writer {} no longer owns cluster {}: {}", config_name_,
            cluster_name, error_details);
  stats_.ownership_lost_.inc();
  completeWarmingUpdate(
      cluster_name, state,
      envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_OwnershipLost,
      error_details);
  setOwned(state, false);
  eraseIfUnused(cluster_name);
}

void DynamicModuleClusterWriter::completeWarmingUpdate(
    absl::string_view cluster_name, ClusterState& state,
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
    absl::string_view error_details) {
  if (state.warming_update_id != 0) {
    completeUpdate(std::exchange(state.warming_update_id, 0), cluster_name, status, error_details);
  }
}

void DynamicModuleClusterWriter::completeUpdate(
    uint64_t update_id, absl::string_view cluster_name,
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
    absl::string_view error_details) {
  stats_.updates_pending_.dec();
  switch (status) {
  case envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Active:
    stats_.update_active_.inc();
    break;
  case envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded:
    stats_.update_superseded_.inc();
    break;
  case envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Removed:
    stats_.update_removed_.inc();
    break;
  case envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed:
    stats_.update_failed_.inc();
    break;
  case envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_OwnershipLost:
    stats_.update_ownership_lost_.inc();
    break;
  }
  if (update_done_ == nullptr) {
    return;
  }
  undelivered_outcomes_.push_back(
      {update_id, std::string(cluster_name), status, std::string(error_details)});
  // Outcomes reach the module in a later loop iteration, never from inside a module call.
  if (!deliver_callback_->enabled()) {
    deliver_callback_->scheduleCallbackNextIteration();
  }
}

void DynamicModuleClusterWriter::deliverOutcomes() {
  std::vector<UpdateOutcome> outcomes;
  outcomes.swap(undelivered_outcomes_);
  for (const UpdateOutcome& outcome : outcomes) {
    update_done_(outcome.update_id, outcome.cluster_name, outcome.status, outcome.error_details);
  }
}

void DynamicModuleClusterWriter::eraseIfUnused(absl::string_view cluster_name) {
  auto it = clusters_.find(cluster_name);
  if (it != clusters_.end() && !it->second.owned && it->second.queued == queue_.end() &&
      it->second.warming_update_id == 0) {
    clusters_.erase(it);
  }
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

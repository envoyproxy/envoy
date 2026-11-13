#include "source/extensions/bootstrap/dynamic_modules/listener_writer.h"

#include <chrono>
#include <utility>

#include "envoy/config/listener/v3/listener.pb.validate.h"
#include "envoy/init/manager.h"

#include "source/common/common/fmt.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

static_assert(static_cast<int>(WriteOutcome::Active) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Active);
static_assert(static_cast<int>(WriteOutcome::Superseded) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Superseded);
static_assert(static_cast<int>(WriteOutcome::Removed) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Removed);
static_assert(static_cast<int>(WriteOutcome::Failed) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Failed);
static_assert(static_cast<int>(WriteOutcome::OwnershipLost) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_update_status_OwnershipLost);
static_assert(static_cast<int>(RemovalResult::Success) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_Success);
static_assert(static_cast<int>(RemovalResult::NotAllowed) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_NotAllowed);
static_assert(static_cast<int>(RemovalResult::NotFound) ==
              envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_NotFound);

namespace {

// The status of a listen failure does not say whether Envoy removed the listener.
std::string listenFailureDetails(absl::string_view listener_name, bool removed) {
  return removed ? fmt::format("listener {} failed to listen and was removed", listener_name)
                 : fmt::format("listener {} failed to listen and its active version keeps serving",
                               listener_name);
}

} // namespace

DynamicModuleListenerWriter::DynamicModuleListenerWriter(
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateDoneCallback update_done)
    : ResourceWriter(extension_name, context, std::move(update_done),
                     {"listener_writer", "listeners_owned"},
                     Config::getTypeUrl<envoy::config::listener::v3::Listener>()),
      init_target_(absl::StrCat("dynamic_modules_bootstrap_listener_writer:", config_name_),
                   [this]() { onClustersInitialized(); }),
      workers_started_callback_(context.mainThreadDispatcher().createSchedulableCallback(
          [this]() { onWorkersStarted(); })),
      init_target_check_callback_(context.mainThreadDispatcher().createSchedulableCallback(
          [this]() { maybeSignalInitTargetReady(); })) {}

void DynamicModuleListenerWriter::onServerInitialized(Server::ListenerManager& listener_manager) {
  listener_manager_ = &listener_manager;
  server_initialized_ = true;
  // The server init manager starts once Envoy initialized its clusters, and its targets hold back
  // the workers, as the initial LDS response does.
  if (context_.initManager().state() != Init::Manager::State::Initialized) {
    context_.initManager().add(init_target_);
  } else {
    onClustersInitialized();
  }
}

void DynamicModuleListenerWriter::onClustersInitialized() {
  // Only a running server starts its init manager, and it calls onShutdown() before it frees the
  // listener manager.
  if (!shut_down_) {
    listener_update_callbacks_handle_ = listener_manager_->addListenerUpdateCallbacks(*this);
  }
  startApplying();
  maybeSignalInitTargetReady();
}

void DynamicModuleListenerWriter::onModuleInitialized() {
  if (!init_cutoff_.has_value()) {
    init_cutoff_ = nextSequence();
  }
  maybeSignalInitTargetReady();
}

void DynamicModuleListenerWriter::maybeSignalInitTargetReady() {
  const std::optional<uint64_t> earliest_queued_sequence = earliestQueuedSequence();
  if (clustersInitialized() && init_cutoff_.has_value() &&
      (!earliest_queued_sequence.has_value() || *earliest_queued_sequence >= *init_cutoff_)) {
    init_target_.ready();
  }
}

void DynamicModuleListenerWriter::onPostInit() {
  if (!shut_down_) {
    workers_started_callback_->scheduleCallbackNextIteration();
  }
}

void DynamicModuleListenerWriter::onWorkersStarted() {
  for (auto& [listener_name, state] : states_) {
    if (state.waits_for_workers) {
      state.waits_for_workers = false;
      completeWarmingUpdate(listener_name, state, WriteOutcome::Active);
    }
  }
}

void DynamicModuleListenerWriter::onShutdown() {
  // The listener manager goes away before this writer does.
  listener_update_callbacks_handle_.reset();
  workers_started_callback_->cancel();
  init_target_check_callback_->cancel();
  shutDown();
}

envoy_dynamic_module_type_bootstrap_listener_writer_update_result
DynamicModuleListenerWriter::addOrUpdateListener(absl::string_view listener_name,
                                                 absl::string_view listener,
                                                 absl::string_view version_info,
                                                 uint64_t& update_id) {
  return queueWrite(listener_name, listener, version_info, update_id)
             ? envoy_dynamic_module_type_bootstrap_listener_writer_update_result_Success
             : envoy_dynamic_module_type_bootstrap_listener_writer_update_result_NotAllowed;
}

envoy_dynamic_module_type_bootstrap_listener_writer_removal_result
DynamicModuleListenerWriter::removeListener(absl::string_view listener_name) {
  const RemovalResult result = queueRemoval(listener_name);
  if (result == RemovalResult::Success) {
    // The removal can cancel a write that holds back Envoy, which then finishes initializing in a
    // later loop iteration instead of inside the call of the module.
    init_target_check_callback_->scheduleCallbackNextIteration();
  }
  return static_cast<envoy_dynamic_module_type_bootstrap_listener_writer_removal_result>(result);
}

void DynamicModuleListenerWriter::onApplyingOperation() {
  // The writes that wait for the workers are active before any later write applies, also when the
  // workers start inside a pass.
  if (workers_started_callback_->enabled()) {
    workers_started_callback_->cancel();
    onWorkersStarted();
  }
}

void DynamicModuleListenerWriter::onPassApplied() { maybeSignalInitTargetReady(); }

absl::Status
DynamicModuleListenerWriter::validateUpdate(const WriterOperation& operation,
                                            const ListenerWriterState& state,
                                            envoy::config::listener::v3::Listener& listener) {
  if (operation.name.empty()) {
    return absl::InvalidArgumentError("listener names must not be empty");
  }
  RETURN_IF_NOT_OK(parseAndValidate(operation, "listener", listener));
  if (listener.has_api_listener()) {
    return absl::InvalidArgumentError(
        fmt::format("listener {} is an API listener", operation.name));
  }
  if (!state.owned && listenerExists(operation.name)) {
    return absl::AlreadyExistsError(
        fmt::format("listener {} exists and is not owned by this extension", operation.name));
  }
  return absl::OkStatus();
}

bool DynamicModuleListenerWriter::listenerExists(absl::string_view listener_name) {
  for (const Network::ListenerConfig& listener : listener_manager_->listeners(
           Server::ListenerManager::WARMING | Server::ListenerManager::ACTIVE)) {
    if (listener.name() == listener_name) {
      return true;
    }
  }
  return false;
}

void DynamicModuleListenerWriter::applyUpdate(WriterOperation& operation) {
  ListenerWriterState& state = states_.find(operation.name)->second;
  envoy::config::listener::v3::Listener listener;
  if (absl::Status status = validateUpdate(operation, state, listener); !status.ok()) {
    ENVOY_LOG_PERIODIC(
        warn, std::chrono::seconds(10),
        "dynamic module listener writer {} rejected update {} of listener {} at version {}: {}",
        config_name_, operation.update_id, operation.name, operation.version_info,
        status.message());
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed, status.message());
    eraseIfUnused(operation.name);
    return;
  }

  // The callbacks that the listener manager runs inside this call belong to this write.
  const uint64_t hash = MessageUtil::hash(listener);
  const uint64_t previous_warming_update_id = std::exchange(state.warming_update_id, 0);
  const bool previous_waits_for_workers = std::exchange(state.waits_for_workers, false);
  const auto supersede_previous_warming_update = [&]() {
    if (previous_warming_update_id != 0) {
      completeUpdate(previous_warming_update_id, operation.name, WriteOutcome::Superseded);
    }
  };
  applying_listener_activated_ = false;
  applying_listener_failed_ = false;
  applying_listener_removed_ = false;
  const absl::StatusOr<bool> update_or_error = applyWithManager(operation.name, [&]() {
    return listener_manager_->addOrUpdateListener(listener, operation.version_info,
                                                  /*modifiable=*/true);
  });

  if (!update_or_error.ok()) {
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(10),
                       "dynamic module listener writer {} failed to apply update {} of listener {} "
                       "at version {}: {}",
                       config_name_, operation.update_id, operation.name, operation.version_info,
                       update_or_error.status().message());
    // The listener manager keeps the warming and active listeners when it rejects a listener.
    state.warming_update_id = previous_warming_update_id;
    state.waits_for_workers = previous_waits_for_workers;
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed,
                   fmt::format("the listener manager rejected listener {}", operation.name));
    eraseIfUnused(operation.name);
    return;
  }

  if (applying_listener_failed_) {
    // The listener warmed and failed to listen inside the listener manager call.
    supersede_previous_warming_update();
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed,
                   listenFailureDetails(operation.name, applying_listener_removed_));
    if (applying_listener_removed_) {
      forgetListener(operation.name, state);
    }
    return;
  }

  if (applying_listener_activated_ ||
      (update_or_error.value() && previous_warming_update_id != 0 && state.active_hash == hash)) {
    // The listener became active inside the listener manager call, or the listener manager
    // dropped the warming listener since the active one matches this write.
    setOwned(state, true);
    supersede_previous_warming_update();
    state.active_hash = hash;
    if (listener_manager_->isWorkerStarted()) {
      completeUpdate(operation.update_id, operation.name, WriteOutcome::Active);
    } else {
      // The workers get the listener when they start.
      state.warming_update_id = operation.update_id;
      state.warming_hash = hash;
      state.waits_for_workers = true;
    }
    return;
  }

  if (update_or_error.value()) {
    setOwned(state, true);
    supersede_previous_warming_update();
    state.warming_update_id = operation.update_id;
    state.warming_hash = hash;
    return;
  }

  // The listener manager left the listener unchanged.
  if (previous_warming_update_id != 0 && state.warming_hash == hash) {
    // This write replaces the identical one that warms or waits for the workers.
    completeUpdate(previous_warming_update_id, operation.name, WriteOutcome::Superseded);
    state.warming_update_id = operation.update_id;
    state.waits_for_workers = previous_waits_for_workers;
    return;
  }
  if (previous_warming_update_id == 0 && state.active_hash == hash) {
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Active);
    return;
  }
  state.warming_update_id = previous_warming_update_id;
  state.waits_for_workers = previous_waits_for_workers;
  completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed,
                 fmt::format("the listener manager refused listener {}, since its listeners are "
                             "stopped",
                             operation.name));
  eraseIfUnused(operation.name);
}

void DynamicModuleListenerWriter::applyRemoval(const WriterOperation& operation) {
  ListenerWriterState& state = states_.find(operation.name)->second;
  if (state.owned) {
    // Give up ownership first so that the removal does not look like a takeover.
    completeWarmingUpdate(operation.name, state, WriteOutcome::Removed);
    forgetListener(operation.name, state);
    listener_manager_->removeListener(operation.name);
    return;
  }
  eraseIfUnused(operation.name);
}

void DynamicModuleListenerWriter::onListenerAddOrUpdate(absl::string_view listener_name,
                                                        const Network::ListenerConfig&) {
  if (listener_name == applyingName()) {
    applying_listener_activated_ = true;
    return;
  }
  auto it = states_.find(listener_name);
  if (it == states_.end() || !it->second.owned) {
    return;
  }
  ListenerWriterState& state = it->second;
  if (state.warming_update_id != 0 && !state.waits_for_workers) {
    state.active_hash = state.warming_hash;
    completeWarmingUpdate(listener_name, state, WriteOutcome::Active);
    return;
  }
  loseOwnership(listener_name, state,
                fmt::format("listener {} was replaced by another component", listener_name));
}

void DynamicModuleListenerWriter::onListenerRemoval(const std::string& listener_name) {
  // A removal inside the listener manager call follows a failure that applyUpdate() reports.
  if (listener_name == applyingName()) {
    return;
  }
  auto it = states_.find(listener_name);
  if (it == states_.end() || !it->second.owned) {
    return;
  }
  loseOwnership(listener_name, it->second,
                fmt::format("listener {} was removed by another component", listener_name));
}

void DynamicModuleListenerWriter::onListenerListenFailure(absl::string_view listener_name,
                                                          bool removed) {
  if (listener_name == applyingName()) {
    applying_listener_failed_ = true;
    applying_listener_removed_ = removed;
    return;
  }
  auto it = states_.find(listener_name);
  if (it == states_.end() || it->second.warming_update_id == 0) {
    return;
  }
  ListenerWriterState& state = it->second;
  completeWarmingUpdate(listener_name, state, WriteOutcome::Failed,
                        listenFailureDetails(listener_name, removed));
  if (removed) {
    // The listener manager removes the listener right after this.
    forgetListener(listener_name, state);
  }
}

void DynamicModuleListenerWriter::forgetListener(absl::string_view listener_name,
                                                 ListenerWriterState& state) {
  setOwned(state, false);
  state.waits_for_workers = false;
  state.active_hash.reset();
  eraseIfUnused(listener_name);
}

void DynamicModuleListenerWriter::loseOwnership(absl::string_view listener_name,
                                                ListenerWriterState& state,
                                                absl::string_view error_details) {
  ENVOY_LOG(warn, "dynamic module listener writer {} no longer owns listener {}: {}", config_name_,
            listener_name, error_details);
  stats_.ownership_lost_.inc();
  completeWarmingUpdate(listener_name, state, WriteOutcome::OwnershipLost, error_details);
  forgetListener(listener_name, state);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

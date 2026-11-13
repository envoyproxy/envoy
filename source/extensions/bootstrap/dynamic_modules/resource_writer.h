#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/writer_shared_state.h"

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

// The outcome of a write. The update status enums of the writers in the ABI use the same values for
// the outcomes they report.
enum class WriteOutcome { Active = 0, Superseded = 1, Removed = 2, Failed = 3, OwnershipLost = 4 };

// The result of a removal. The removal result enums of the writers in the ABI use the same values.
enum class RemovalResult { Success = 0, NotAllowed = 1, NotFound = 2 };

// How long an apply pass may run before it yields to the event loop.
inline constexpr std::chrono::milliseconds WriterApplyPassBudget{5};

// A queued write or removal of a resource.
struct WriterOperation {
  std::string name;
  // The sequence numbers of the first and of the latest submission for the resource.
  uint64_t first_sequence;
  uint64_t sequence;
  // The ID of the write, or 0 for a removal.
  uint64_t update_id;
  std::string resource;
  std::string version_info;
};

// What a writer knows about a resource that it owns or has queued an operation for.
struct WriterResourceState {
  explicit WriterResourceState(std::list<WriterOperation>::iterator queue_end)
      : queued(queue_end) {}

  bool owned = false;
  // The queued operation of the resource, or the end of the queue when none is queued.
  std::list<WriterOperation>::iterator queued;
  // The applied write that waits for the resource to become active, or 0.
  uint64_t warming_update_id = 0;
};

// The names of the statistics of a writer.
struct WriterStatNames {
  // The prefix after dynamic_modules, for example cluster_writer.
  absl::string_view prefix;
  // The gauge of the resources that the module owns, for example clusters_owned.
  absl::string_view owned;
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
  Stats::Gauge& owned_;
  Stats::Gauge& updates_pending_;
};

WriterStats generateWriterStats(Stats::Scope& scope, absl::string_view config_name,
                                const WriterStatNames& names);

/**
 * Queues the writes and removals that a bootstrap dynamic module submits per resource name,
 * applies them on the main thread in passes, and reports the outcome of every accepted write
 * through the done callback in a later loop iteration. A writer derives from it to apply the
 * operations. It sets server_initialized_ once the server is initialized, calls startApplying()
 * once Envoy initialized its clusters, passes every write to its manager through
 * applyWithManager(), and calls shutDown() when shutdown begins. All methods must be called on the
 * main thread.
 */
template <class State, class Status>
class ResourceWriter : public Logger::Loggable<Logger::Id::dynamic_modules> {
  static_assert(std::is_base_of_v<WriterResourceState, State>);

public:
  // Reports the outcome of a write. The string views are valid only during the call.
  using UpdateDoneCallback = std::function<void(uint64_t update_id, absl::string_view name,
                                                Status status, absl::string_view error_details)>;

  virtual ~ResourceWriter() = default;

protected:
  /**
   * @param extension_name the name of the bootstrap extension.
   * @param context the server factory context.
   * @param update_done the callback that reports outcomes, or nullptr when the module has no done
   * hook.
   * @param stat_names the names of the statistics.
   * @param resource_type_url the type URL of the resources, whose dependent xDS types a pass holds
   * back.
   */
  ResourceWriter(absl::string_view extension_name,
                 Server::Configuration::ServerFactoryContext& context,
                 UpdateDoneCallback update_done, const WriterStatNames& stat_names,
                 absl::string_view resource_type_url)
      : config_name_(extension_name.empty() ? "default" : extension_name), context_(context),
        stats_scope_(context.serverScope().store().createScope("")),
        stats_(generateWriterStats(*stats_scope_, config_name_, stat_names)),
        resource_type_url_(resource_type_url), shared_state_(WriterSharedState::get(context)),
        update_done_(std::move(update_done)),
        apply_callback_(context.mainThreadDispatcher().createSchedulableCallback(
            [this]() { applyQueuedOperations(); })),
        deliver_callback_(context.mainThreadDispatcher().createSchedulableCallback(
            [this]() { deliverOutcomes(); })) {}

  // Applies a write. The writer completes it, or leaves it warming.
  virtual void applyUpdate(WriterOperation& operation) PURE;
  // Applies a removal.
  virtual void applyRemoval(const WriterOperation& operation) PURE;
  // Called before a pass applies an operation. It must not queue or remove operations.
  virtual void onApplyingOperation() {}
  // Called once a pass applied its operations.
  virtual void onPassApplied() {}

  /**
   * Queues a write.
   * @param update_id is set to the ID of the write, or to 0 when the write is not allowed.
   * @return whether the write is allowed.
   */
  bool queueWrite(absl::string_view name, absl::string_view resource,
                  absl::string_view version_info, uint64_t& update_id) {
    update_id = 0;
    if (!writesAllowed()) {
      stats_.update_rejected_.inc();
      return false;
    }

    State& state = states_.try_emplace(name, queue_.end()).first->second;
    update_id = next_update_id_++;
    stats_.update_submitted_.inc();
    stats_.updates_pending_.inc();
    const uint64_t sequence = next_sequence_++;
    if (state.queued == queue_.end()) {
      state.queued = queue_.insert(
          queue_.end(), WriterOperation{std::string(name), sequence, sequence, update_id,
                                        std::string(resource), std::string(version_info)});
    } else {
      // The queued operation keeps its place in the queue and takes the latest write.
      WriterOperation& operation = *state.queued;
      if (operation.update_id != 0) {
        completeUpdate(operation.update_id, name, WriteOutcome::Superseded);
      }
      operation.sequence = sequence;
      operation.update_id = update_id;
      operation.resource = std::string(resource);
      operation.version_info = std::string(version_info);
    }
    scheduleApplyPass();
    return true;
  }

  // Queues a removal. A queued write of the resource completes as Removed.
  RemovalResult queueRemoval(absl::string_view name) {
    if (!writesAllowed()) {
      return RemovalResult::NotAllowed;
    }
    auto it = states_.find(name);
    if (it == states_.end()) {
      return RemovalResult::NotFound;
    }

    State& state = it->second;
    if (state.queued == queue_.end()) {
      const uint64_t sequence = next_sequence_++;
      state.queued = queue_.insert(
          queue_.end(), WriterOperation{std::string(name), sequence, sequence, 0, {}, {}});
    } else {
      WriterOperation& operation = *state.queued;
      if (operation.update_id != 0) {
        completeUpdate(operation.update_id, name, WriteOutcome::Removed);
      }
      // A resource that is neither owned nor being applied has nothing to remove, so cancel the
      // queued write.
      if (!state.owned && name != applying_name_) {
        queue_.erase(state.queued);
        state.queued = queue_.end();
        eraseIfUnused(name);
        return RemovalResult::Success;
      }
      operation.sequence = next_sequence_++;
      operation.update_id = 0;
      operation.resource = std::string();
      operation.version_info = std::string();
    }
    scheduleApplyPass();
    return RemovalResult::Success;
  }

  // Starts the passes once Envoy finished initializing its static and CDS clusters.
  void startApplying() {
    clusters_initialized_ = true;
    scheduleApplyPass();
  }

  bool clustersInitialized() const { return clusters_initialized_; }
  // The sequence number of the next submission.
  uint64_t nextSequence() const { return next_sequence_; }
  // The first submission of the queued operation that waits the longest, if any.
  std::optional<uint64_t> earliestQueuedSequence() const {
    // The queue keeps the order of the first submissions.
    return queue_.empty() ? std::nullopt : std::make_optional(queue_.front().first_sequence);
  }

  /**
   * Parses and validates the resource of a write like its xDS API does, but keeps the contents of
   * the resource out of the error.
   * @param kind names the type of the resource in the error, for example cluster.
   */
  template <class Proto>
  absl::Status parseAndValidate(const WriterOperation& operation, absl::string_view kind,
                                Proto& resource) const {
    if (!resource.ParseFromString(operation.resource)) {
      return absl::InvalidArgumentError(fmt::format("failed to parse {} {}", kind, operation.name));
    }
    if (resource.name() != operation.name) {
      return absl::InvalidArgumentError(fmt::format("{} {} is named {} in its configuration", kind,
                                                    operation.name, resource.name()));
    }
    ProtobufMessage::ValidationVisitor& validation_visitor =
        context_.messageValidationContext().dynamicValidationVisitor();
    TRY_ASSERT_MAIN_THREAD {
      if (!validation_visitor.skipValidation()) {
        MessageUtil::checkForUnexpectedFields(resource, validation_visitor);
      }
      MessageUtil::validateDurationFields(resource);
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      return absl::InvalidArgumentError(
          fmt::format("invalid {} {}: {}", kind, operation.name, e.what()));
    });
    std::string error;
    if (!Validate(resource, &error)) {
      return absl::InvalidArgumentError(
          fmt::format("invalid {} {}: {}", kind, operation.name, error));
    }
    return absl::OkStatus();
  }

  /**
   * Passes a write of the resource to the manager of the writer. A removal of the resource from
   * inside the call is queued instead of cancelling the write.
   * @param call makes the call of the manager, which can throw.
   */
  template <class Call>
  absl::StatusOr<bool> applyWithManager(absl::string_view name, const Call& call) {
    applying_name_ = name;
    absl::StatusOr<bool> update_or_error = absl::UnknownError("not applied");
    TRY_ASSERT_MAIN_THREAD { update_or_error = call(); }
    END_TRY
    CATCH(const EnvoyException& e, { update_or_error = absl::InvalidArgumentError(e.what()); });
    applying_name_ = {};
    return update_or_error;
  }

  // The name of the resource whose write applyWithManager() passes to the manager, or empty.
  absl::string_view applyingName() const { return applying_name_; }

  void setOwned(State& state, bool owned) {
    if (state.owned == owned) {
      return;
    }
    state.owned = owned;
    if (owned) {
      stats_.owned_.inc();
    } else {
      stats_.owned_.dec();
    }
  }

  void completeWarmingUpdate(absl::string_view name, State& state, WriteOutcome outcome,
                             absl::string_view error_details = {}) {
    if (state.warming_update_id != 0) {
      completeUpdate(std::exchange(state.warming_update_id, 0), name, outcome, error_details);
    }
  }

  void completeUpdate(uint64_t update_id, absl::string_view name, WriteOutcome outcome,
                      absl::string_view error_details = {}) {
    stats_.updates_pending_.dec();
    switch (outcome) {
    case WriteOutcome::Active:
      stats_.update_active_.inc();
      break;
    case WriteOutcome::Superseded:
      stats_.update_superseded_.inc();
      break;
    case WriteOutcome::Removed:
      stats_.update_removed_.inc();
      break;
    case WriteOutcome::Failed:
      stats_.update_failed_.inc();
      break;
    case WriteOutcome::OwnershipLost:
      stats_.update_ownership_lost_.inc();
      break;
    }
    if (update_done_ == nullptr) {
      return;
    }
    undelivered_outcomes_.push_back(
        {update_id, std::string(name), outcome, std::string(error_details)});
    // Outcomes reach the module in a later loop iteration, never from inside a module call.
    if (!deliver_callback_->enabled()) {
      deliver_callback_->scheduleCallbackNextIteration();
    }
  }

  void eraseIfUnused(absl::string_view name) {
    auto it = states_.find(name);
    if (it != states_.end() && !it->second.owned && it->second.queued == queue_.end() &&
        it->second.warming_update_id == 0) {
      states_.erase(it);
    }
  }

  // Refuses later writes and drops the queued work.
  void shutDown() {
    shut_down_ = true;
    apply_callback_->cancel();
    deliver_callback_->cancel();
    shared_state_->onShutdown(this);
    states_.clear();
    queue_.clear();
    undelivered_outcomes_.clear();
    stats_.owned_.set(0);
    stats_.updates_pending_.set(0);
  }

  // The extension name, or "default" when it is empty.
  const std::string config_name_;
  Server::Configuration::ServerFactoryContext& context_;

private:
  // Owned, since the server scope goes away before the bootstrap extensions at shutdown.
  const Stats::ScopeSharedPtr stats_scope_;

protected:
  WriterStats stats_;
  // Node based, so that references stay valid while module hooks queue more operations.
  absl::node_hash_map<std::string, State> states_;
  bool server_initialized_ = false;
  bool shut_down_ = false;

private:
  // The outcome of a write that is not reported to the module yet.
  struct UpdateOutcome {
    uint64_t update_id;
    std::string name;
    WriteOutcome outcome;
    std::string error_details;
  };

  bool writesAllowed() const { return server_initialized_ && !shut_down_; }

  void scheduleApplyPass() {
    // Passes run in separate event loop iterations, so a module that writes again from its done
    // hook cannot keep the loop from polling.
    if (clusters_initialized_ && !queue_.empty() && !apply_callback_->enabled()) {
      apply_callback_->scheduleCallbackNextIteration();
    }
  }

  void applyQueuedOperations() {
    TimeSource& time_source = context_.mainThreadDispatcher().timeSource();
    // Send the requests of the xDS types that a burst of written resources depends on together, as
    // the xDS API of the resources does.
    shared_state_->pauseDependentTypes(resource_type_url_, context_.xdsManager(),
                                       time_source.monotonicTime());
    // Operations queued while this pass runs wait for the next pass, so that every pass ends.
    const uint64_t sequence_cutoff = next_sequence_;
    const MonotonicTime deadline = time_source.monotonicTime() + WriterApplyPassBudget;
    bool applied_any = false;
    while (true) {
      auto it = std::find_if(queue_.begin(), queue_.end(),
                             [sequence_cutoff](const WriterOperation& operation) {
                               return operation.sequence < sequence_cutoff;
                             });
      if (it == queue_.end() || (applied_any && time_source.monotonicTime() >= deadline)) {
        break;
      }
      onApplyingOperation();
      WriterOperation operation = std::move(*it);
      queue_.erase(it);
      states_.find(operation.name)->second.queued = queue_.end();
      if (operation.update_id == 0) {
        applyRemoval(operation);
      } else {
        applyUpdate(operation);
      }
      applied_any = true;
    }
    shared_state_->onApplyPassDone(this, queue_.empty(), time_source.monotonicTime());
    onPassApplied();
    scheduleApplyPass();
  }

  void deliverOutcomes() {
    std::vector<UpdateOutcome> outcomes;
    outcomes.swap(undelivered_outcomes_);
    for (const UpdateOutcome& outcome : outcomes) {
      update_done_(outcome.update_id, outcome.name, static_cast<Status>(outcome.outcome),
                   outcome.error_details);
    }
  }

  // The queued operations in the order of their first submission.
  std::list<WriterOperation> queue_;
  uint64_t next_sequence_ = 0;
  // Set once Envoy finished initializing its static and CDS clusters.
  bool clusters_initialized_ = false;
  absl::string_view applying_name_;
  const std::string resource_type_url_;
  const std::shared_ptr<WriterSharedState> shared_state_;
  const UpdateDoneCallback update_done_;
  std::vector<UpdateOutcome> undelivered_outcomes_;
  uint64_t next_update_id_ = 1;
  Event::SchedulableCallbackPtr apply_callback_;
  Event::SchedulableCallbackPtr deliver_callback_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

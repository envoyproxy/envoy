#pragma once

#include <list>

#include "envoy/init/manager.h"

#include "source/common/common/logger.h"
#include "source/common/init/watcher_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Init {

/**
 * Init::ManagerImpl coordinates initialization of one or more "targets." See comments in
 * envoy/init/manager.h for an overview.
 *
 * When the logging level is set to "debug" or "trace," the log will contain entries for all
 * significant events in the initialization flow:
 *
 *   - Targets added to the manager
 *   - Initialization started for the manager and for each target
 *   - Initialization completed for each target and for the manager
 *   - Destruction of targets and watchers
 *   - Callbacks to "unavailable" (deleted) targets, manager, or watchers
 */
class ManagerImpl : public Manager, Logger::Loggable<Logger::Id::init> {
public:
  /**
   * @param name a human-readable manager name, for logging / debugging.
   */
  ManagerImpl(absl::string_view name);

  // Init::Manager
  State state() const override;
  void add(const Target& target) override;
  void initialize(const Watcher& watcher) override;
  void dumpUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps& dumps) override;

private:
  // Callback function with an additional target_name parameter, decrease unready targets count by
  // 1, update target_names_count_ hash map.
  void onTargetReady(absl::string_view target_name);

  void ready();

  // Human-readable name for logging.
  const std::string name_;

  // Current state.
  State state_{State::Uninitialized};

  // Current number of registered targets that have not yet initialized.
  uint32_t count_{0};

  // Handle to the watcher passed in `initialize`, to be called when initialization completes.
  WatcherHandlePtr watcher_handle_;

  // Watcher to receive ready notifications from each target. We restrict the watcher_ inside
  // ManagerImpl to be constructed with the 'TargetAwareReadyFn' fn so that the init manager will
  // get target name information when the watcher_ calls 'onTargetSendName(target_name)' For any
  // other purpose, a watcher can be constructed with either TargetAwareReadyFn or ReadyFn.
  const WatcherImpl watcher_;

  // All registered targets.
  std::list<TargetHandlePtr> target_handles_;

  // Count of target_name of unready targets.
  absl::flat_hash_map<std::string, uint32_t> target_names_count_;
};

} // namespace Init
} // namespace Envoy

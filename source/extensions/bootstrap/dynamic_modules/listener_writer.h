#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/listener_manager.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/bootstrap/dynamic_modules/resource_writer.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

// What the listener writer knows about a listener that it owns or has queued an operation for.
struct ListenerWriterState : public WriterResourceState {
  using WriterResourceState::WriterResourceState;

  // Whether the warming write was applied before the workers started, so it is active once they
  // start.
  bool waits_for_workers = false;
  // The hash of the configuration of the write that warms or waits for the workers.
  uint64_t warming_hash = 0;
  // The hash of the configuration that the active listener of the extension serves, if any.
  std::optional<uint64_t> active_hash;
};

/**
 * Adds, updates and removes the listeners of a bootstrap dynamic module. Writes are queued and
 * applied on the main thread in later event loop iterations, and the outcome of every accepted
 * write is reported through the done callback. All methods must be called on the main thread.
 */
class DynamicModuleListenerWriter
    : public ResourceWriter<ListenerWriterState,
                            envoy_dynamic_module_type_bootstrap_listener_writer_update_status>,
      public Server::ListenerUpdateCallbacks {
public:
  /**
   * @param extension_name the name of the bootstrap extension.
   * @param context the server factory context.
   * @param update_done the callback that reports outcomes, or nullptr when the module has no done
   * hook.
   */
  DynamicModuleListenerWriter(absl::string_view extension_name,
                              Server::Configuration::ServerFactoryContext& context,
                              UpdateDoneCallback update_done);

  /**
   * Called once the server is initialized. Writes are not allowed before this.
   */
  void onServerInitialized(Server::ListenerManager& listener_manager);

  /**
   * Called when the module signals that its initialization is complete. Envoy finishes
   * initializing only once the writes submitted before this are applied.
   */
  void onModuleInitialized();

  /**
   * Called once Envoy finished initializing, right before it starts its workers.
   */
  void onPostInit();

  /**
   * Called when shutdown begins. Refuses later writes and drops the queued work.
   */
  void onShutdown();

  /**
   * Queues a write of the given serialized listener.
   * @param update_id is set to the ID of the write, or to 0 when the write is rejected.
   */
  envoy_dynamic_module_type_bootstrap_listener_writer_update_result
  addOrUpdateListener(absl::string_view listener_name, absl::string_view listener,
                      absl::string_view version_info, uint64_t& update_id);

  /**
   * Queues the removal of the given listener.
   */
  envoy_dynamic_module_type_bootstrap_listener_writer_removal_result
  removeListener(absl::string_view listener_name);

  // Server::ListenerUpdateCallbacks
  void onListenerAddOrUpdate(absl::string_view listener_name,
                             const Network::ListenerConfig& listener_config) override;
  void onListenerRemoval(const std::string& listener_name) override;
  void onListenerListenFailure(absl::string_view listener_name, bool removed) override;

private:
  // ResourceWriter
  void applyUpdate(WriterOperation& operation) override;
  void applyRemoval(const WriterOperation& operation) override;
  void onApplyingOperation() override;
  void onPassApplied() override;

  void onClustersInitialized();
  void onWorkersStarted();
  void maybeSignalInitTargetReady();
  absl::Status validateUpdate(const WriterOperation& operation, const ListenerWriterState& state,
                              envoy::config::listener::v3::Listener& listener);
  bool listenerExists(absl::string_view listener_name);
  void forgetListener(absl::string_view listener_name, ListenerWriterState& state);
  void loseOwnership(absl::string_view listener_name, ListenerWriterState& state,
                     absl::string_view error_details);

  Server::ListenerManager* listener_manager_ = nullptr;
  // The first sequence number that the module submitted after it signaled that its initialization
  // is complete.
  std::optional<uint64_t> init_cutoff_;
  // What happened to the listener being applied inside the listener manager call.
  bool applying_listener_activated_ = false;
  bool applying_listener_failed_ = false;
  bool applying_listener_removed_ = false;
  Init::TargetImpl init_target_;
  Event::SchedulableCallbackPtr workers_started_callback_;
  // Rechecks the startup hold after a removal.
  Event::SchedulableCallbackPtr init_target_check_callback_;
  Server::ListenerUpdateCallbacksHandlePtr listener_update_callbacks_handle_;
};

using DynamicModuleListenerWriterPtr = std::unique_ptr<DynamicModuleListenerWriter>;

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

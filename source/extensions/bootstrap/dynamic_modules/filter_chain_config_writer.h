#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/listener/v3/listener_components.pb.h"
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

/**
 * Adds, updates and removes the filter chains of a bootstrap dynamic module, which the listeners
 * with fcds_config find by name. Writes are queued and applied on the main thread in later event
 * loop iterations, and the outcome of every accepted write is reported through the done callback.
 * All methods must be called on the main thread.
 */
class DynamicModuleFilterChainConfigWriter
    : public ResourceWriter<
          WriterResourceState,
          envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status>,
      public Server::ListenerUpdateCallbacks {
public:
  /**
   * @param extension_name the name of the bootstrap extension.
   * @param context the server factory context.
   * @param update_done the callback that reports outcomes, or nullptr when the module has no done
   * hook.
   */
  DynamicModuleFilterChainConfigWriter(absl::string_view extension_name,
                                       Server::Configuration::ServerFactoryContext& context,
                                       UpdateDoneCallback update_done);

  /**
   * Called once the server is initialized. Writes are not allowed before this.
   */
  void onServerInitialized(Server::ListenerManager& listener_manager);

  /**
   * Called when shutdown begins. Refuses later writes and drops the queued work.
   */
  void onShutdown();

  /**
   * Queues a write of the given serialized filter chain.
   * @param update_id is set to the ID of the write, or to 0 when the write is rejected.
   */
  envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result
  addOrUpdateFilterChain(absl::string_view filter_chain_name, absl::string_view filter_chain,
                         uint64_t& update_id);

  /**
   * Queues the removal of the given filter chain.
   */
  envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result
  removeFilterChain(absl::string_view filter_chain_name);

  // Server::ListenerUpdateCallbacks
  void onListenerAddOrUpdate(absl::string_view, const Network::ListenerConfig&) override {}
  void onListenerRemoval(const std::string&) override {}
  void onFilterChainAddOrUpdate(absl::string_view filter_chain_name) override;

private:
  // ResourceWriter
  void applyUpdate(WriterOperation& operation) override;
  void applyRemoval(const WriterOperation& operation) override;

  void onClustersInitialized();
  absl::Status validateUpdate(const WriterOperation& operation,
                              envoy::config::listener::v3::FilterChain& filter_chain) const;

  Server::ListenerManager* listener_manager_ = nullptr;
  Init::TargetImpl init_target_;
  Server::ListenerUpdateCallbacksHandlePtr listener_update_callbacks_handle_;
};

using DynamicModuleFilterChainConfigWriterPtr =
    std::unique_ptr<DynamicModuleFilterChainConfigWriter>;

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

#pragma once

#include <memory>

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/common/wasm/plugin.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::Context;
using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginHandleThreadLocal;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;

class FilterConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
               Server::Configuration::FactoryContext& context);

  FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
               Server::Configuration::UpstreamFactoryContext& context);

  std::shared_ptr<Context> createFilter() {
    if (!tls_slot_->currentThreadRegistered()) {
      return nullptr;
    }

    auto handle_holder = tls_slot_->get();
    if (!handle_holder.has_value() || !handle_holder->handle()) {
      return nullptr;
    }
    PluginHandleSharedPtr& handle = handle_holder->handle();

    Wasm* wasm = handle->wasmHandle() ? handle->wasmHandle()->wasm().get() : nullptr;

    // If the loaded WASM instance has already been loaded and not failed, use the WASM instance
    // directly.
    if (wasm && !wasm->isFailed()) {
      // Create a Context with the loaded WASM instance.
      return std::make_shared<Context>(wasm, handle->rootContextId(), handle);
    }

    // If the WASM instance has failed, then handle the failure according to the failure policy.
    switch (plugin_->failurePolicy()) {
    case Common::Wasm::FailurePolicy::FAIL_CLOSED:
      // Create a Context with empty WASM instance which will fail all calls.
      return std::make_shared<Context>(nullptr, 0, handle);
    case Common::Wasm::FailurePolicy::FAIL_IGNORE:
      // Fail ignore skips adding this filter to callbacks.
      return nullptr;
    case Common::Wasm::FailurePolicy::FAIL_RELOAD: {
      // Fail reload will reload the WASM plugin.
      handle = Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm_, plugin_,
                                                          handle_holder->workerDispatcher());
      if (handle == nullptr) {
        ENVOY_LOG(warn, "Failed to reload WASM plugin: {}", plugin_->name_);
        return std::make_shared<Context>(nullptr, 0, handle);
      }
      wasm = handle->wasmHandle() ? handle->wasmHandle()->wasm().get() : nullptr;
      if (!wasm || wasm->isFailed()) {
        ENVOY_LOG(warn, "Failed to reload WASM plugin: {}", plugin_->name_);
        return std::make_shared<Context>(nullptr, 0, handle);
      }
      // Create a Context with the reloaded WASM instance.
      return std::make_shared<Context>(wasm, handle->rootContextId(), handle);
    }
    default:
      return nullptr;
    }
  }

private:
  void createWasm(Server::Configuration::ServerFactoryContext& server, Stats::ScopeSharedPtr scope,
                  Init::Manager& init_manager);

  // WASM independent raw configuration.
  PluginSharedPtr plugin_;
  // Base WASM instance that is used to create the thread-local WASM instance.
  Common::Wasm::WasmHandleSharedPtr base_wasm_;
  // Per-thread WASM plugin handle that contains the WASM instance and loaded configuration.
  ThreadLocal::TypedSlotPtr<PluginHandleThreadLocal> tls_slot_;
  RemoteAsyncDataProviderPtr remote_data_provider_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

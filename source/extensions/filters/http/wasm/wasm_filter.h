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
using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtrThreadLocal;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;

class FilterConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
               Server::Configuration::FactoryContext& context);

  FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
               Server::Configuration::UpstreamFactoryContext& context);

  std::shared_ptr<Context> createFilter() {
    Wasm* wasm = nullptr;
    if (!tls_slot_->currentThreadRegistered()) {
      return nullptr;
    }
    PluginHandleSharedPtr handle = tls_slot_->get()->handle();
    if (!handle) {
      return nullptr;
    }
    if (handle->wasmHandle()) {
      wasm = handle->wasmHandle()->wasm().get();
    }
    if (!wasm || wasm->isFailed()) {
      if (handle->plugin()->fail_open_) {
        return nullptr; // Fail open skips adding this filter to callbacks.
      } else {
        return std::make_shared<Context>(nullptr, 0,
                                         handle); // Fail closed is handled by an empty Context.
      }
    }
    return std::make_shared<Context>(wasm, handle->rootContextId(), handle);
  }

private:
  void createWasm(PluginSharedPtr plugin,
                  Envoy::Server::Configuration::ServerFactoryContext& server,
                  const Stats::ScopeSharedPtr& scope, Envoy::Init::Manager& init_manager);
  ThreadLocal::TypedSlotPtr<PluginHandleSharedPtrThreadLocal> tls_slot_;
  RemoteAsyncDataProviderPtr remote_data_provider_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

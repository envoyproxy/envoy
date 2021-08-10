#pragma once

#include <memory>

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/common/wasm/plugin.h"
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

  std::shared_ptr<Context> createFilter() {
    Wasm* wasm = nullptr;
    PluginHandleSharedPtr handle = tls_slot_->get()->handle();
    if (handle->wasmHandle()) {
      wasm = handle->wasmHandle()->wasm().get();
    }
    if (!wasm || wasm->isFailed()) {
      if (handle->plugin()->fail_open_) {
        // Fail open skips adding this filter to callbacks.
        return nullptr;
      } else {
        // Fail closed is handled by an empty Context.
        return std::make_shared<Context>(nullptr, 0, handle);
      }
    }
    return std::make_shared<Context>(wasm, handle->rootContextId(), handle);
  }

private:
  ThreadLocal::TypedSlotPtr<PluginHandleSharedPtrThreadLocal> tls_slot_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

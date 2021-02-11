#pragma once

#include <memory>

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/common/wasm/wasm.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::Context;
using Envoy::Extensions::Common::Wasm::PluginHandle;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;

class FilterConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& proto_config,
               Server::Configuration::FactoryContext& context);

  std::shared_ptr<Context> createFilter() {
    Wasm* wasm = nullptr;
    auto handle = tls_slot_->get();
    if (handle.has_value()) {
      wasm = handle->wasm().get();
    }
    if (!wasm || wasm->isFailed()) {
      if (plugin_->fail_open_) {
        // Fail open skips adding this filter to callbacks.
        return nullptr;
      } else {
        // Fail closed is handled by an empty Context.
        return std::make_shared<Context>(nullptr, 0, plugin_);
      }
    }
    return std::make_shared<Context>(wasm, handle->rootContextId(), plugin_);
  }

private:
  PluginSharedPtr plugin_;
  ThreadLocal::TypedSlotPtr<PluginHandle> tls_slot_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

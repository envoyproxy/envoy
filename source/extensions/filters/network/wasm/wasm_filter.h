#pragma once

#include <memory>

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::Context;
using Envoy::Extensions::Common::Wasm::PluginHandleManager;
using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;

class FilterConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
               Server::Configuration::FactoryContext& context);

  std::shared_ptr<Context> createFilter() {
    auto manager = tls_slot_->get();
    PluginHandleSharedPtr plugin_handle = manager->handle();
    if (!plugin_handle) {
      if (tls_slot_->get()->tryRestartPlugin()) {
        plugin_handle = manager->handle();
      }
    }

    if (!plugin_handle || plugin_handle->isFailed()) {
      if (fail_open_) {
        // Fail open skips adding this filter to callbacks.
        return nullptr;
      } else {
        // Fail closed is handled by an empty Context.
        return std::make_shared<Context>(nullptr, fail_open_);
      }
    }
    return std::make_shared<Context>(plugin_handle, fail_open_);
  }

  Wasm* wasmForTest() { return tls_slot_->get()->handle()->wasmHandle()->wasm().get(); }

private:
  bool fail_open_{false};
  ThreadLocal::TypedSlotPtr<PluginHandleManager> tls_slot_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

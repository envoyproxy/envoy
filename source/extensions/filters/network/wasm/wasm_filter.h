#pragma once

#include <memory>

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/common/wasm/wasm.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::Context;
using Envoy::Extensions::Common::Wasm::Wasm;
using Envoy::Extensions::Common::Wasm::WasmHandle;

class FilterConfig : Logger::Loggable<Logger::Id::wasm> {
public:
  FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
               Server::Configuration::FactoryContext& context);

  std::shared_ptr<Context> createFilter() {
    Wasm* wasm = nullptr;
    if (tls_slot_->get()) {
      wasm = tls_slot_->getTyped<WasmHandle>().wasm().get();
    }
    if (plugin_->fail_open_ && (!wasm || wasm->isFailed())) {
      return nullptr;
    }
    if (wasm && !root_context_id_) {
      root_context_id_ = wasm->getRootContext(plugin_->root_id_)->id();
    }
    return std::make_shared<Context>(wasm, root_context_id_, plugin_);
  }
  Envoy::Extensions::Common::Wasm::Wasm* wasm() {
    return tls_slot_->getTyped<WasmHandle>().wasm().get();
  }

private:
  uint32_t root_context_id_{0};
  Envoy::Extensions::Common::Wasm::PluginSharedPtr plugin_;
  ThreadLocal::SlotPtr tls_slot_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

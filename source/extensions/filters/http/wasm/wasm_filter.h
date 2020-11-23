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
    if (plugin_->fail_open_ && (!wasm || wasm->isFailed())) {
      return nullptr;
    }
    if (wasm && !root_context_id_) {
      root_context_id_ = wasm->getRootContext(plugin_, false)->id();
    }
    return std::make_shared<Context>(wasm, root_context_id_, plugin_);
  }

private:
  uint32_t root_context_id_{0};
  PluginSharedPtr plugin_;
  ThreadLocal::TypedSlotPtr<PluginHandle> tls_slot_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

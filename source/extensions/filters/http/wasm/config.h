#pragma once

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.h"
#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

/**
 * Config registration for the Wasm filter. @see NamedHttpFilterConfigFactory.
 */
class WasmFilterConfig
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::wasm::v3::Wasm> {
public:
  WasmFilterConfig()
      : Common::UnifiedFactoryBase<envoy::extensions::filters::http::wasm::v3::Wasm>(
            "envoy.filters.http.wasm") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::wasm::v3::Wasm& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    context.api().customStatNamespaces().registerStatNamespace(
        Extensions::Common::Wasm::CustomStatNamespace);
    auto filter_config = std::make_shared<FilterConfig>(proto_config, context, extra_context);
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      auto filter = filter_config->createContext();
      if (!filter) { // Fail open
        return;
      }
      callbacks.addStreamFilter(filter);
      callbacks.addAccessLogHandler(filter);
    };
  }
};

using UpstreamWasmFilterConfig = WasmFilterConfig;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

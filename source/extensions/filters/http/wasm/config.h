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
    : public Common::CommonFactoryBase<envoy::extensions::filters::http::wasm::v3::Wasm>,
      public Server::Configuration::NamedHttpFilterConfigFactory,
      public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  WasmFilterConfig()
      : Common::CommonFactoryBase<envoy::extensions::filters::http::wasm::v3::Wasm>(
            "envoy.filters.http.wasm") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::wasm::v3::Wasm&>(
            proto_config, context.messageValidationVisitor()),
        stats_prefix, context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::wasm::v3::Wasm&&>(
            proto_config, context.serverFactoryContext().messageValidationVisitor()),
        stats_prefix, context);
  }

private:
  template <class FactoryContext>
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::wasm::v3::Wasm& proto_config, const std::string&,
      FactoryContext& context) {
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        Extensions::Common::Wasm::CustomStatNamespace);
    auto filter_config = std::make_shared<FilterConfig>(proto_config, context);
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

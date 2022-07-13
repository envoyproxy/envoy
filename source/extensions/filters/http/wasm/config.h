#pragma once

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.h"
#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

/**
 * Config registration for the Wasm filter. @see NamedHttpFilterConfigFactory.
 */
class WasmFilterConfig
    : public Common::DownstreamFactoryBase<envoy::extensions::filters::http::wasm::v3::Wasm> {
public:
  WasmFilterConfig() : DownstreamFactoryBase("envoy.filters.http.wasm") {}

private:
  Http::FilterFactoryCb createDownstreamFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::wasm::v3::Wasm& proto_config, const std::string&,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.h"
#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

constexpr char WasmName[] = "envoy.filters.network.wasm";

/**
 * Config registration for the Wasm filter. @see NamedNetworkFilterConfigFactory.
 */
class WasmFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::network::wasm::v3::Wasm> {
public:
  WasmFilterConfig() : FactoryBase(WasmName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

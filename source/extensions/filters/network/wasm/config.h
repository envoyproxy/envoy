#pragma once

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.h"
#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

/**
 * Config registration for the Wasm filter. @see NamedNetworkFilterConfigFactory.
 */
class WasmFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::network::wasm::v3::Wasm> {
public:
  WasmFilterConfig() : FactoryBase(NetworkFilterNames::get().Wasm) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

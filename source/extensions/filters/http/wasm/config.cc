#include "source/extensions/filters/http/wasm/config.h"

#include "envoy/extensions/filters/http/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

/**
 * Static registration for the Wasm filter. @see RegisterFactory.
 */
REGISTER_FACTORY(WasmFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamWasmFilterConfig, Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

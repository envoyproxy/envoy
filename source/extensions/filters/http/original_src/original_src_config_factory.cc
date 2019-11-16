#include "extensions/filters/http/original_src/original_src_config_factory.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/original_src/config.h"
#include "extensions/filters/http/original_src/original_src.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

Http::FilterFactoryCb OriginalSrcConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::original_src::v2alpha1::OriginalSrc& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  Config config(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<OriginalSrcFilter>(config));
  };
}

/**
 * Static registration for the original_src filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalSrcConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

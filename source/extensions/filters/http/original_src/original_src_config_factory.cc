#include "source/extensions/filters/http/original_src/original_src_config_factory.h"

#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"
#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/original_src/config.h"
#include "source/extensions/filters/http/original_src/original_src.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

Http::FilterFactoryCb OriginalSrcConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::original_src::v3::OriginalSrc& proto_config,
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

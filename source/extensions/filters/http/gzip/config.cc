#include "extensions/filters/http/gzip/config.h"

#include "extensions/filters/http/gzip/gzip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

Http::FilterFactoryCb GzipFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::gzip::v3::Gzip& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  Common::Compressors::CompressorFilterConfigSharedPtr config = std::make_shared<GzipFilterConfig>(
      proto_config, stats_prefix, context.scope(), context.runtime());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Common::Compressors::CompressorFilter>(config));
  };
}

/**
 * Static registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(GzipFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.gzip"};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

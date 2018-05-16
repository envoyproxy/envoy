#include "extensions/filters/http/gzip/config.h"

#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/gzip/gzip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

Http::FilterFactoryCb GzipFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::gzip::v2::Gzip& proto_config, const std::string&,
    Server::Configuration::FactoryContext&) {
  GzipFilterConfigSharedPtr config = std::make_shared<GzipFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GzipFilter>(config));
  };
}

/**
 * Static registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
static Registry::RegisterFactory<GzipFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

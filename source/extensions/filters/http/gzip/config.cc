#include "extensions/filters/http/gzip/config.h"

#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/gzip/gzip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

Http::FilterFactoryCb
GzipFilterFactory::createFilter(const envoy::config::filter::http::gzip::v2::Gzip& proto_config) {
  GzipFilterConfigSharedPtr config = std::make_shared<GzipFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GzipFilter>(config));
  };
}

Http::FilterFactoryCb
GzipFilterFactory::createFilterFactory(const Json::Object&, const std::string&,
                                       Server::Configuration::FactoryContext&) {
  NOT_IMPLEMENTED;
}

Http::FilterFactoryCb
GzipFilterFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                const std::string&,
                                                Server::Configuration::FactoryContext&) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::gzip::v2::Gzip&>(
          proto_config));
}

/**
 * Static registration for the gzip filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GzipFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

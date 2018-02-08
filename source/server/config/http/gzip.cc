#include "server/config/http/gzip.h"

#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/http/filter/gzip_filter.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
GzipFilterConfig::createFilter(const envoy::config::filter::http::gzip::v2::Gzip& proto_config) {
  Http::GzipFilterConfigSharedPtr config = std::make_shared<Http::GzipFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http::GzipFilter>(config));
  };
}

HttpFilterFactoryCb GzipFilterConfig::createFilterFactory(const Json::Object&, const std::string&,
                                                          FactoryContext&) {
  NOT_IMPLEMENTED;
}

HttpFilterFactoryCb
GzipFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                               const std::string&, FactoryContext&) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::gzip::v2::Gzip&>(
          proto_config));
}

/**
 * Static registration for the gzip filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GzipFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy

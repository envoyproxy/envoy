#include "server/config/http/gzip.h"

#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/gzip_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
GzipFilterConfig::createFilter(const envoy::config::filter::http::gzip::v2::Gzip& gzip) {
  Http::GzipFilterConfigSharedPtr config(new Http::GzipFilterConfig(gzip));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Http::GzipFilter(config)});
  };
}

HttpFilterFactoryCb GzipFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                          const std::string&, FactoryContext&) {
  envoy::config::filter::http::gzip::v2::Gzip proto_config;
  Config::FilterJson::translateGzipFilter(json_config, proto_config);
  return createFilter(proto_config);
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

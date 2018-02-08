#include "server/config/http/gzip.h"

#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/http/filter/gzip_filter.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
GzipFilterConfig::createFilter(const envoy::config::filter::http::gzip::v2::Gzip& gzip) {
  Http::GzipFilterConfigSharedPtr config(new Http::GzipFilterConfig(gzip));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http::GzipFilter>(config));
  };
}

HttpFilterFactoryCb GzipFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                          const std::string&, FactoryContext&) {
  envoy::config::filter::http::gzip::v2::Gzip proto_config;
  MessageUtil::loadFromJson(json_config.asJsonString(), proto_config);
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

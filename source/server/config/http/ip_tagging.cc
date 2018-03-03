#include "server/config/http/ip_tagging.h"

#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/http/filter/ip_tagging_filter.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb IpTaggingFilterConfig::createFilterFactory(const Json::Object&,
                                                               const std::string&,
                                                               FactoryContext&) {
  NOT_IMPLEMENTED;
}

HttpFilterFactoryCb
IpTaggingFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                    const std::string& stat_prefix,
                                                    FactoryContext& context) {

  Http::IpTaggingFilterConfigSharedPtr config(new Http::IpTaggingFilterConfig(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::ip_tagging::v2::IPTagging&>(proto_config),
      stat_prefix, context.scope(), context.runtime()));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Http::IpTaggingFilter>(config));
  };
}

/**
 * Static registration for the ip tagging filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpTaggingFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy

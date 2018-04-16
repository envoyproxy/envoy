#include "extensions/filters/http/ip_tagging/config.h"

#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

Server::Configuration::HttpFilterFactoryCb
IpTaggingFilterFactory::createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) {
  NOT_IMPLEMENTED;
}

Server::Configuration::HttpFilterFactoryCb IpTaggingFilterFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& context) {

  IpTaggingFilterConfigSharedPtr config(new IpTaggingFilterConfig(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::ip_tagging::v2::IPTagging&>(proto_config),
      stat_prefix, context.scope(), context.runtime()));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<IpTaggingFilter>(config));
  };
}

/**
 * Static registration for the ip tagging filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpTaggingFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "extensions/filters/http/ip_tagging/config.h"

#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

Http::FilterFactoryCb IpTaggingFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {

  IpTaggingFilterConfigSharedPtr config(
      new IpTaggingFilterConfig(proto_config, stat_prefix, context.scope(), context.runtime()));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<IpTaggingFilter>(config));
  };
}

/**
 * Static registration for the ip tagging filter. @see RegisterFactory.
 */
REGISTER_FACTORY(IpTaggingFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.ip_tagging"};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

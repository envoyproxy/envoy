#include "source/extensions/filters/http/ip_tagging/config.h"

#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ip_tagging/ip_tagging_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

absl::StatusOr<Http::FilterFactoryCb> IpTaggingFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {

  absl::StatusOr<IpTaggingFilterConfigSharedPtr> config = IpTaggingFilterConfig::create(
      proto_config, stat_prefix, context.scope(), context.serverFactoryContext().runtime());
  RETURN_IF_NOT_OK_REF(config.status());
  return
      [config = std::move(config.value())](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(std::make_shared<IpTaggingFilter>(config));
      };
}

/**
 * Static registration for the ip tagging filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(IpTaggingFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.ip_tagging");

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

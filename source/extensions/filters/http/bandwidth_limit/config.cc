#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

absl::StatusOr<Http::FilterFactoryCb> BandwidthLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();

  absl::StatusOr<FilterConfigSharedPtr> filter_config = FilterConfig::create(
      proto_config, context.scope(), server_context.runtime(), server_context.timeSource());
  RETURN_IF_NOT_OK_REF(filter_config.status());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<BandwidthLimiter>(*filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
BandwidthLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return FilterConfig::create(proto_config, context.scope(), context.runtime(),
                              context.timeSource(), true);
}

/**
 * Static registration for the bandwidth limit filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(BandwidthLimitFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.bandwidth_limit");

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

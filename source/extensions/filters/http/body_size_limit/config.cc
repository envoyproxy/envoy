#include "source/extensions/filters/http/body_size_limit/config.h"

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/body_size_limit/body_size_limit_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

namespace {
Http::FilterFactoryCb createFilterFactory(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
    Stats::Scope& scope, const std::string& stats_prefix) {
  auto filter_config =
      std::make_shared<BodySizeLimitFilterConfig>(proto_config, scope, stats_prefix);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BodySizeLimitFilter>(filter_config));
  };
}
} // namespace

absl::StatusOr<Http::FilterFactoryCb>
BodySizeLimitFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  return createFilterFactory(proto_config, extra_context.statsPrefixScopeOr(context),
                             extra_context.statsPrefixOr());
}

/**
 * Static registration for the body size limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(BodySizeLimitFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamBodySizeLimitFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

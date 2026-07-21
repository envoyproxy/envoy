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
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config) {
  BodySizeLimitFilterConfigSharedPtr filter_config(new BodySizeLimitFilterConfig(proto_config));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<BodySizeLimitFilter>(filter_config));
  };
}
} // namespace

absl::StatusOr<Http::FilterFactoryCb> BodySizeLimitFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
    const std::string&, DualInfo, Server::Configuration::ServerFactoryContext&) {
  return createFilterFactory(proto_config);
}

absl::StatusOr<Http::FilterFactoryCb>
BodySizeLimitFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
    const std::string&, Server::Configuration::ServerFactoryContext&) {
  return createFilterFactory(proto_config);
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

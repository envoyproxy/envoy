#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include <memory>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

Http::FilterFactoryCb GcpAuthnFilterFactory::createFilterFactoryFromProtoTyped(
    const GcpAuthnFilterConfig& config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<TokenCache> token_cache;
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.cache_config(), cache_size, 0) > 0) {
    token_cache = std::make_shared<TokenCache>(config.cache_config(), context);
  }
  FilterConfigSharedPtr filter_config =
      std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
          config);

  return [config, stats_prefix, &context, token_cache = std::move(token_cache),
          filter_config =
              std::move(filter_config)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GcpAuthnFilter>(
        filter_config, context, stats_prefix,
        (token_cache != nullptr) ? &token_cache->tls.get()->cache() : nullptr));
  };
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GcpAuthnFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

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
  // Life time of this cache is critical!!!!
  // Store it in the filter factory??
  // We should only need one slot in tls ???
  // auto token_cache = new TokenCache(config, context);
  auto token_cache = std::make_shared<TokenCache>(config, context);
  return [config, &context, &stats_prefix, token_cache = std::move(token_cache)](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    // TokenCacheImpl& cache = token_cache->tls.get()->cache();
    // callbacks.addStreamFilter(std::make_shared<GcpAuthnFilter>(config, context, stats_prefix,
    // &cache));
    callbacks.addStreamFilter(std::make_shared<GcpAuthnFilter>(config, context, stats_prefix,
                                                               &token_cache->tls.get()->cache()));
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

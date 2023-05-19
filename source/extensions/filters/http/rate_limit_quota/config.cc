#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

Http::FilterFactoryCb RateLimitQuotaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig&
        filter_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  // Filter config const object is created on the main thread and shared between worker threads.
  FilterConfigConstSharedPtr config = std::make_shared<
      envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig>(
      filter_config);
  return
      [config = std::move(config), &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<RateLimitQuotaFilter>(config, context));
      };
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitQuotaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

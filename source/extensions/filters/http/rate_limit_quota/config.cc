#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

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

  // Quota bucket TLS object is created on the main thread and shared between worker threads.
  std::shared_ptr<QuotaBucket> bucket_cache = std::make_shared<QuotaBucket>(context);
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(config->rlqs_server());
  return [config = std::move(config), &context, bucket_cache = std::move(bucket_cache),
          config_with_hash_key](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<RateLimitQuotaFilter>(
        config, context, bucket_cache->tls.get()->quotaBuckets(),
        bucket_cache->tls.get()->rateLimitClient(), config_with_hash_key));
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

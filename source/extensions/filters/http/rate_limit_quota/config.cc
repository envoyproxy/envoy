#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket.h"

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
  //   RateLimitClientSharedPtr shared_client =
  //       createRateLimitGrpcClient(context, config->rlqs_server());
  //   std::shared_ptr<BucketCache> bucket_cache = std::make_shared<BucketCache>(context,
  //   shared_client);
  std::shared_ptr<BucketCache> bucket_cache = std::make_shared<BucketCache>(context);

  return [config = std::move(config), &context, bucket_cache = std::move(bucket_cache)](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<RateLimitQuotaFilter>(
        config, context, &(bucket_cache->tls.get()->buckets()),
        &(bucket_cache->tls.get()->quotaUsageReports())));
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

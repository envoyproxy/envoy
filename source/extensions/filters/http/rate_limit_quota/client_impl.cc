#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include <cstddef>
#include <memory>

#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using BucketAction = RateLimitQuotaResponse::BucketAction;

void LocalRateLimitClientImpl::createBucket(const BucketId& bucket_id, size_t id,
                                            const BucketAction& initial_bucket_action,
                                            bool initial_request_allowed) {
  std::shared_ptr<GlobalRateLimitClientImpl> global_client = getGlobalClient();
  // Intentionally crash if the local client is initialized with a null global
  // client or TLS slot due to a bug.
  global_client->createBucket(bucket_id, id, initial_bucket_action, initial_request_allowed);
}

std::shared_ptr<CachedBucket> LocalRateLimitClientImpl::getBucket(size_t id) {
  std::shared_ptr<BucketsCache> buckets_cache = getBucketsCache();
  // Intentionally crash if the client is initialized with a null global cache
  // or TLS slot due to a bug.
  auto bucket_it = buckets_cache->find(id);
  return (bucket_it != buckets_cache->end()) ? bucket_it->second : nullptr;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

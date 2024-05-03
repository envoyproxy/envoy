#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"
#include "envoy/common/token_bucket.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local_object.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;

using BucketAction = ::envoy::service::rate_limit_quota::v3::
    RateLimitQuotaResponse::BucketAction;
using BucketQuotaUsage = ::envoy::service::rate_limit_quota::v3::
    RateLimitQuotaUsageReports::BucketQuotaUsage;

struct QuotaUsage {
  QuotaUsage(uint64_t num_requests_allowed, uint64_t num_requests_denied,
             std::chrono::nanoseconds last_report)
      : num_requests_allowed(num_requests_allowed),
        num_requests_denied(num_requests_denied),
        last_report(last_report) {}

  // Requests allowed.
  std::atomic<uint64_t> num_requests_allowed;
  // Requests throttled.
  std::atomic<uint64_t> num_requests_denied;
  // Last report time. Should be visible to worker threads so they can check if
  // reporting timing is going correctly. Should only be updated by main thread.
  std::atomic<std::chrono::nanoseconds> last_report;
};

// This object stores the data for single bucket entry. The usage cache & action
// cache are in separate pointers to enable separate pointer-swapping.
struct CachedBucket {
  CachedBucket(
      const BucketId& bucket_id, std::shared_ptr<QuotaUsage> quota_usage,
      const BucketAction& bucket_action,
      std::shared_ptr<::Envoy::TokenBucket> token_bucket_limiter)
      : bucket_id(bucket_id),
        quota_usage(quota_usage),
        bucket_action(bucket_action),
        token_bucket_limiter(token_bucket_limiter) {}

  // BucketId object that is associated with this bucket. It is part of the
  // report that is sent to RLQS server.
  BucketId bucket_id;
  // Aggregated usage of the ID'd bucket for the next report cycle.
  std::shared_ptr<QuotaUsage> quota_usage;
  // Cached action from the RLQS server's last response that gave an updated
  // assignment for this ID'd bucket.
  BucketAction bucket_action;
  // Rate limiter based on token bucket algorithm. Shared to make a single
  // TokenBucket reusable when copying & pointer-swapping the overall cache.
  std::shared_ptr<::Envoy::TokenBucket> token_bucket_limiter;
};

/**
 * An interface for a client used in the RateLimitQuotaService (RLQS) filter
 * worker threads to safely access & modify global resources.
 */
class RateLimitClient {
 public:
  virtual ~RateLimitClient() = default;

  // Safe creation & getting of global buckets.
  virtual void createBucket(const BucketId& bucket_id, size_t id,
                            const BucketAction& initial_bucket_action,
                            bool initial_request_allowed) PURE;
  virtual std::shared_ptr<CachedBucket> getBucket(size_t id) PURE;
};

// shared_ptr<CachedBucket> must be passed around to guarantee the
// CachedBucket's existence for the duration of its use in a local thread after
// getting it via getBucket.
using BucketsCache = absl::flat_hash_map<size_t, std::shared_ptr<CachedBucket>>;

class ThreadLocalBucketsCache : public Envoy::ThreadLocal::ThreadLocalObject,
                                Logger::Loggable<Logger::Id::rate_limit_quota> {
 public:
  ThreadLocalBucketsCache(std::shared_ptr<BucketsCache> quota_buckets)
      : quota_buckets_(quota_buckets) {}

  // Thread-unsafe operations like index creation should only be done by the
  // global ThreadLocalClient.
  std::shared_ptr<BucketsCache> quota_buckets_;
};

}  // namespace RateLimitQuota
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy

#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>

#include "envoy/common/token_bucket.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"
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

using BucketAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::BucketAction;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;

struct QuotaUsage {
  QuotaUsage(uint64_t num_requests_allowed, uint64_t num_requests_denied,
             std::chrono::nanoseconds last_report)
      : num_requests_allowed(num_requests_allowed), num_requests_denied(num_requests_denied),
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
  CachedBucket(const BucketId& bucket_id, std::shared_ptr<QuotaUsage> quota_usage,
               std::unique_ptr<BucketAction> cached_action,
               std::shared_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
               std::chrono::milliseconds fallback_ttl, const BucketAction& default_action,
               std::shared_ptr<::Envoy::TokenBucket> token_bucket_limiter)
      : bucket_id(bucket_id), quota_usage(quota_usage), cached_action(std::move(cached_action)),
        fallback_action(fallback_action), fallback_ttl(fallback_ttl),
        default_action(default_action), token_bucket_limiter(token_bucket_limiter) {}

  // BucketId object that is associated with this bucket. It is part of the
  // report that is sent to RLQS server.
  BucketId bucket_id;

  // Aggregated usage of the ID'd bucket for the next report cycle.
  std::shared_ptr<QuotaUsage> quota_usage;

  // Cached action from the RLQS server's last response that gave an updated
  // assignment for this ID'd bucket. Can be null if no assignment has been
  // received yet or if the previous assignment has passed its time-to-live.
  std::unique_ptr<BucketAction> cached_action;

  // Set in the bucket config's expired_assignment_behavior, this fallback
  // action has its own TTL and determines behavior after a cached assignment
  // expires. After this action's TTL passes the bucket reverts back to its
  // default_action.
  std::shared_ptr<envoy::type::v3::RateLimitStrategy> fallback_action;
  std::chrono::milliseconds fallback_ttl;
  // Default action defined by the bucket's no_assignment_behavior setting. Used
  // when the bucket is waiting for an assigned action from the RLQS server
  // (e.g. during initial bucket hits & after stale assignments expire).
  BucketAction default_action;

  // Action expiration timer when assignments haven't been sent for this bucket
  // for too long & the current assignment passes its TTL. This triggers a
  // bucket write-operation so the timer triggers and calls back to operations
  // on the main thread (through the global client).
  Envoy::Event::TimerPtr action_expiration_timer = nullptr;
  // Fallback expiration timer for when the cached_action has expired and the
  // fallback_action has its own TTL.
  Envoy::Event::TimerPtr fallback_expiration_timer = nullptr;

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
                            const BucketAction& default_bucket_action,
                            std::unique_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
                            std::chrono::milliseconds fallback_ttl,
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

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;
using GrpcAsyncClient =
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// A RateLimitClient that should be created locally in each worker thread. It
// knows to write by posting GlobalRateLimitClientImpl calls to the main thread
// and it knows how to read current values from TLS.
class LocalRateLimitClientImpl : public RateLimitClient,
                                 public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  explicit LocalRateLimitClientImpl(
      GlobalRateLimitClientImpl* global_client,
      Envoy::ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_cache_tls)
      : global_client_(global_client), buckets_cache_tls_(buckets_cache_tls) {}

  void createBucket(const BucketId& bucket_id, size_t id, const BucketAction& default_bucket_action,
                    std::unique_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
                    std::chrono::milliseconds fallback_ttl, bool initial_request_allowed) override;
  // Note: returns null if the global resources (client or bucket) are
  // unavailable. Resource creation is left to createBucket(). The CachedBucket*
  // is safe to reference so long as the local client exists.
  std::shared_ptr<CachedBucket> getBucket(size_t id) override;

private:
  inline std::shared_ptr<BucketsCache> getBucketsCache() {
    return (buckets_cache_tls_.get().has_value()) ? buckets_cache_tls_.get()->quota_buckets_
                                                  : nullptr;
  }

  // Lockless access to global resources via TLS.
  GlobalRateLimitClientImpl* global_client_;
  ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_cache_tls_;
};

inline std::unique_ptr<RateLimitClient>
createLocalRateLimitClient(GlobalRateLimitClientImpl* global_client,
                           ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_cache_tls_) {
  return std::make_unique<LocalRateLimitClientImpl>(global_client, buckets_cache_tls_);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

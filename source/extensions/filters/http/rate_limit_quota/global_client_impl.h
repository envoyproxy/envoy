#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

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
using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Callbacks to trigger when the main thread finishes executing a queued
// operation. Primarily used for testing.
class GlobalRateLimitClientCallbacks {
public:
  virtual ~GlobalRateLimitClientCallbacks() = default;
  virtual void onBucketCreated(const BucketId& bucket_id, size_t id) = 0;
  // Called on success or failure to send the actual message.
  virtual void onUsageReportsSent() = 0;
  virtual void onQuotaResponseProcessed() = 0;
  virtual void onActionExpiration() = 0;
  virtual void onFallbackExpiration() = 0;
};

// Grpc bidirectional streaming client which handles the communication with
// RLQS server. A pointer to it should go into TLS as it should be referenced by
// worker threads' local RateLimitClients.
class GlobalRateLimitClientImpl : public Grpc::AsyncStreamCallbacks<
                                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                                  public Event::DeferredDeletable,
                                  public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  GlobalRateLimitClientImpl(const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                            Server::Configuration::FactoryContext& context,
                            absl::string_view domain_name,
                            std::chrono::milliseconds send_reports_interval,
                            ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
                            Envoy::Event::Dispatcher& main_dispatcher);
  ~GlobalRateLimitClientImpl() {
    if (stream_ != nullptr) {
      stream_.resetStream();
    }
  }

  void onReceiveMessage(RateLimitQuotaResponsePtr&& response) override;

  // RawAsyncStreamCallbacks methods;
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // DeferredDeletable
  // Cleanup resources that have to be deleted on the main thread before this deferred deletion.
  // Not thread-safe & should only be called by the main thread.
  void deleteIsPending() override;

  // Functions needed by LocalRateLimitClientImpl to make unsafe modifications
  // to global resources. All are non-blocking & safely callable by worker
  // threads and make unsafe changes by ensuring that all such changes are done
  // by the main thread. Pointer swaps to TLS make the resources readable to
  // worker threads' LocalRateLimitClientImpl instances.
  void createBucket(const BucketId& bucket_id, size_t id, const BucketAction& default_bucket_action,
                    std::unique_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
                    std::chrono::milliseconds fallback_ttl, bool initial_request_allowed);

  // Set optional callbacks. Primarily used for testing asynchronous operations.
  // Not thread-safe to call when the client is in-use.
  void setCallbacks(std::unique_ptr<GlobalRateLimitClientCallbacks> callbacks) {
    callbacks_ = std::move(callbacks);
  }

private:
  // Build usage reports (i.e., the request sent to RLQS server) from the
  // buckets in quota bucket cache. If specified, the usage reports will only
  // include the given bucket.
  RateLimitQuotaUsageReports buildReports();
  RateLimitQuotaUsageReports buildReports(std::shared_ptr<CachedBucket> cached_bucket);

  // Helpers to write to TLS.
  // Copy source-of-truth BucketsCache & pointer-swap/write to TLS.
  inline void writeBucketsToTLS() {
    auto tl_buckets_cache =
        std::make_shared<ThreadLocalBucketsCache>(std::make_shared<BucketsCache>(buckets_cache_));
    buckets_tls_.set([tl_buckets_cache]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
      return tl_buckets_cache;
    });
    ENVOY_LOG(debug, "RLQS buckets cache written to TLS.");
  }

  // Helpers to execute in the main thread, triggered by public interfaces or by
  // internal flows.
  void createBucketImpl(const BucketId& bucket_id, size_t id,
                        const BucketAction& default_bucket_action,
                        std::unique_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
                        std::chrono::milliseconds fallback_ttl, bool initial_request_allowed);
  void sendUsageReportImpl(const RateLimitQuotaUsageReports& reports);
  void onQuotaResponseImpl(const RateLimitQuotaResponse* response);
  bool startStreamImpl();
  // When the send-reports timer triggers, a report should be compiled and sent
  // on the stream. If the stream isn't active (e.g. if aborted by the server),
  // it should be restarted.
  void startSendReportsTimerImpl();
  void onSendReportsTimer();

  // Optional callbacks to run after queued operations finish on the main
  // thread.
  std::unique_ptr<GlobalRateLimitClientCallbacks> callbacks_ = nullptr;

  // Note: the following timer functions do not own cached_bucket and only use
  // the pointer to verify that the targeted bucket is still the one in the
  // source-of-truth before making any changes.

  // Expiration timer starts on the main queue when an action assignment is set
  // for a Bucket, based on the assignment's TTL.
  void startActionExpirationTimer(CachedBucket* cached_bucket, size_t id);
  // Replaces the cached bucket in the source-of-truth with a new bucket. If
  // provided, the new bucket's cached_action is the fallback action of the
  // previous bucket, and the fallback ttl timer starts.
  void onActionExpirationTimer(CachedBucket* cached_bucket, size_t id);
  // Fallback timer starts on the main queue when a cached bucket has its
  // assignment expire & the fallback action has to take over for the duration
  // of its own TTL.
  void startFallbackExpirationTimer(CachedBucket* cached_bucket, size_t id);
  // Replaces the cached bucket in the source-of-truth with a new bucket with
  // its cached_action removed.
  void onFallbackExpirationTimer(CachedBucket* cached_bucket, size_t id);

  // Bucket creation should only attempt stream creation once, and after that
  // all attempts to create / recreate the stream should be handled internally
  // to avoid spam.
  bool stream_tried_by_bucket_creation_ = false;
  // Domain from filter configuration. The same domain name throughout the
  // whole lifetime of client.
  std::string domain_name_;
  // Client is stored as the bare object since there is no ownership transfer
  // involved.
  GrpcAsyncClient async_client_;
  Grpc::AsyncStream<RateLimitQuotaUsageReports> stream_{};

  // Reference to TLS slot for the global quota bucket cache. It outlives
  // the filter.
  ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls_;

  // Source-of-truth for the quota bucket cache. All changes to the
  // source-of-truth should be CachedBucket pointer swaps.
  BucketsCache buckets_cache_;

  std::chrono::milliseconds send_reports_interval_;
  TimeSource& time_source_;
  Envoy::Event::Dispatcher& main_dispatcher_;

  // Starts when the filter is hit for the first time. From then on, this
  // timer's trigger ensures the health of the RLQS stream & sends aggregated
  // usage reports.
  Event::TimerPtr send_reports_timer_ = nullptr;
};

/**
 * Create a shared rate limit client. It should be shared to each worker
 * thread via TLS.
 */
inline std::unique_ptr<GlobalRateLimitClientImpl>
createGlobalRateLimitClientImpl(Server::Configuration::FactoryContext& context,
                                absl::string_view domain_name,
                                std::chrono::milliseconds send_reports_interval,
                                ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
                                Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key) {
  Envoy::Event::Dispatcher& main_dispatcher = context.serverFactoryContext().mainThreadDispatcher();
  return std::make_unique<GlobalRateLimitClientImpl>(config_with_hash_key, context, domain_name,
                                                     send_reports_interval, buckets_tls,
                                                     main_dispatcher);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

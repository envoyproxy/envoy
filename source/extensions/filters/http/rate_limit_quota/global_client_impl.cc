#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using BucketAction = RateLimitQuotaResponse::BucketAction;
using envoy::type::v3::RateLimitStrategy;

GlobalRateLimitClientImpl::GlobalRateLimitClientImpl(
    const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
    Server::Configuration::FactoryContext& context, absl::string_view domain_name,
    std::chrono::milliseconds send_reports_interval,
    Envoy::ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
    Envoy::Event::Dispatcher& main_dispatcher)
    : domain_name_(domain_name), buckets_tls_(buckets_tls),
      send_reports_interval_(send_reports_interval),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()),
      main_dispatcher_(main_dispatcher) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  absl::StatusOr<Grpc::AsyncClientFactoryPtr> rlqs_stream_client_factory =
      context.serverFactoryContext()
          .clusterManager()
          .grpcAsyncClientManager()
          .factoryForGrpcService(config_with_hash_key.config(), context.scope(), true);
  if (!rlqs_stream_client_factory.ok()) {
    throw EnvoyException(std::string(rlqs_stream_client_factory.status().message()));
  }

  absl::StatusOr<Grpc::RawAsyncClientPtr> rlqs_stream_client =
      (*rlqs_stream_client_factory)->createUncachedRawAsyncClient();
  if (!rlqs_stream_client.ok()) {
    throw EnvoyException(std::string(rlqs_stream_client.status().message()));
  }
  async_client_ = GrpcAsyncClient(std::move(*rlqs_stream_client));
}

void GlobalRateLimitClientImpl::deleteIsPending() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // Deleting the async client also triggers stream_ to reset, if active.
  // The client & stream must be destroyed before the GlobalRateLimitClientImpl,
  // as it provides the stream callbacks.
  async_client_->reset();
}

void getUsageFromBucket(const CachedBucket& cached_bucket, TimeSource& time_source,
                        BucketQuotaUsage& usage) {
  std::shared_ptr<QuotaUsage> cached_usage = cached_bucket.quota_usage;
  *usage.mutable_bucket_id() = cached_bucket.bucket_id;

  std::atomic<uint64_t>& num_requests_allowed = cached_usage->num_requests_allowed;
  std::atomic<uint64_t>& num_requests_denied = cached_usage->num_requests_denied;

  // Reset usage atomics to 0 and save current values as request totals.
  uint64_t allowed = num_requests_allowed.load(std::memory_order_relaxed);
  while (!num_requests_allowed.compare_exchange_weak(allowed, 0, std::memory_order_relaxed)) {
  }
  uint64_t denied = num_requests_denied.load(std::memory_order_relaxed);
  while (!num_requests_denied.compare_exchange_weak(denied, 0, std::memory_order_relaxed)) {
  }

  usage.set_num_requests_allowed(allowed);
  usage.set_num_requests_denied(denied);

  // Get the time elapsed since this bucket last went into a usage report.
  std::atomic<std::chrono::nanoseconds>& cached_last_report = cached_usage->last_report;
  std::chrono::nanoseconds now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source.monotonicTime().time_since_epoch());
  // `last_report` should always be set in the cached bucket.
  std::chrono::nanoseconds last_report = cached_last_report.load(std::memory_order_relaxed);
  // Update the last_report time point.
  while (!cached_last_report.compare_exchange_weak(last_report, now, std::memory_order_relaxed)) {
  }

  usage.mutable_time_elapsed()->set_seconds(
      std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count());
}

// Read a specific bucket's aggregated usage & build it into a UsageReports
// message.
RateLimitQuotaUsageReports
GlobalRateLimitClientImpl::buildReports(std::shared_ptr<CachedBucket> cached_bucket) {
  RateLimitQuotaUsageReports report;
  getUsageFromBucket(*cached_bucket, time_source_, *report.add_bucket_quota_usages());

  // Set the domain name.
  report.set_domain(domain_name_);
  ENVOY_LOG(debug, "The bucket-specific usage report that will be sent to RLQS server:\n{}",
            report.DebugString());
  return report;
}

// Read all active buckets' aggregated usage & build them into a UsageReports
// message.
RateLimitQuotaUsageReports GlobalRateLimitClientImpl::buildReports() {
  RateLimitQuotaUsageReports report;
  // Build the report from quota bucket source-of-truth. The buckets_cache_ is
  // guaranteed to be safe so long as index deletion & creation only happen in
  // the main thread.
  for (const auto& [_, cached] : buckets_cache_) {
    // If the cached bucket or underlying QuotaUsage are null, it is due to a
    // bug, and should cause a crash.
    std::shared_ptr<QuotaUsage> cached_usage = cached->quota_usage;
    auto* usage = report.add_bucket_quota_usages();
    getUsageFromBucket(*cached, time_source_, *usage);
  }

  // Set the domain name.
  report.set_domain(domain_name_);
  ENVOY_LOG(debug, "The usage report that will be sent to RLQS server:\n{}", report.DebugString());
  return report;
}

void GlobalRateLimitClientImpl::createBucket(const BucketId& bucket_id, size_t id,
                                             const BucketAction& default_bucket_action,
                                             std::unique_ptr<RateLimitStrategy> fallback_action,
                                             std::chrono::milliseconds fallback_ttl,
                                             bool initial_request_allowed) {
  // Mutable to move fallback_action ownership into the main thread then into
  // the created bucket.
  main_dispatcher_.post([&, bucket_id, id, default_bucket_action,
                         fallback_action_ptr = std::move(fallback_action), fallback_ttl,
                         initial_request_allowed]() mutable {
    createBucketImpl(bucket_id, id, default_bucket_action, std::move(fallback_action_ptr),
                     fallback_ttl, initial_request_allowed);
  });
}

void GlobalRateLimitClientImpl::createBucketImpl(const BucketId& bucket_id, size_t id,
                                                 const BucketAction& default_bucket_action,
                                                 std::unique_ptr<RateLimitStrategy> fallback_action,
                                                 std::chrono::milliseconds fallback_ttl,
                                                 bool initial_request_allowed) {
  // On the first createBucket call (so the first time a bucket is hit in the
  // RLQS filter), start the stream & reporting timer.
  if (!stream_tried_by_bucket_creation_) {
    stream_tried_by_bucket_creation_ = true;
    startSendReportsTimerImpl();
    if (startStreamImpl()) {
      ENVOY_LOG(info, "RLQS stream started successfully.");
    } else {
      ENVOY_LOG(error, "RLQS stream failed to start. Usage collection will continue "
                       "regardless while reattempting to open the stream.");
    }
  }

  // If multiple createBucket calls were posted before the bucket was pushed to
  // TLS, just increment the appropriate usage counter.
  if (auto bucket_it = buckets_cache_.find(id); bucket_it != buckets_cache_.end()) {
    // The bucket and underlying QuotaUsage in the source-of-truth should never
    // be null. If there's a bug that creates null entries, then this will
    // crash.
    std::shared_ptr<CachedBucket> bucket = bucket_it->second;
    std::shared_ptr<QuotaUsage> quota_usage = bucket->quota_usage;

    // Increment num_requests_(allowed|denied) based on the allow/deny choice
    // already made by the calling filter.
    std::atomic<uint64_t>& num_requests =
        (initial_request_allowed ? quota_usage->num_requests_allowed
                                 : quota_usage->num_requests_denied);
    uint64_t expected = num_requests.load(std::memory_order_relaxed);
    while (!num_requests.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed)) {
    }
    return;
  }

  // Create new bucket and add it into the source-of-truth then pointer-swap
  // with the BucketsCache already in TLS. Start the QuotaUsage at 1
  // request to count the request that was allowed/denied before calling to
  // CreateBucket. Default the bucket's action assignment to the configured
  // no_assignment_behavior.
  std::chrono::nanoseconds now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source_.monotonicTime().time_since_epoch());

  buckets_cache_[id] = std::make_shared<CachedBucket>(
      bucket_id,
      std::make_shared<QuotaUsage>(initial_request_allowed, !initial_request_allowed, now), nullptr,
      std::move(fallback_action), fallback_ttl, default_bucket_action, nullptr);

  // Send initial usage report for this new bucket to notify the RLQS server of
  // the new bucket's activation.
  RateLimitQuotaUsageReports initial_report = buildReports(buckets_cache_[id]);
  sendUsageReportImpl(initial_report);

  writeBucketsToTLS();
  if (callbacks_ != nullptr) {
    callbacks_->onBucketCreated(buckets_cache_[id]->bucket_id, id);
  }
}

// This helper function reads from the current usage caches & sends the
// resulting reports message over the stream.
void GlobalRateLimitClientImpl::sendUsageReportImpl(const RateLimitQuotaUsageReports& reports) {
  if (stream_ == nullptr) {
    ENVOY_LOG(error, "Attempted to send a UsageReports message across the RLQS stream "
                     "but it has already closed.");
    return;
  }
  stream_->sendMessage(reports, /*end_stream=*/false);
}

bool actionHasTokenBucket(BucketAction* bucket_action) {
  return (bucket_action && bucket_action->has_quota_assignment_action() &&
          bucket_action->quota_assignment_action().has_rate_limit_strategy() &&
          bucket_action->quota_assignment_action().rate_limit_strategy().has_token_bucket());
}

// Check if TokenBuckets are deeply equal.
bool protoTokenBucketsEq(const ::envoy::type::v3::TokenBucket& new_tb,
                         const ::envoy::type::v3::TokenBucket& old_tb) {
  return (new_tb.max_tokens() == old_tb.max_tokens() &&
          new_tb.tokens_per_fill().value() == old_tb.tokens_per_fill().value() &&
          new_tb.fill_interval().nanos() == old_tb.fill_interval().nanos());
}

std::shared_ptr<AtomicTokenBucketImpl>
createTokenBucketFromAction(const RateLimitStrategy& strategy, TimeSource& time_source,
                            const AtomicTokenBucketImpl* existing_token_bucket) {
  const auto& token_bucket = strategy.token_bucket();
  const auto& interval_proto = token_bucket.fill_interval();
  // Convert absl::duration to int64_t seconds
  int64_t fill_interval_sec = absl::ToInt64Seconds(absl::Seconds(interval_proto.seconds()) +
                                                   absl::Nanoseconds(interval_proto.nanos()));
  double fill_rate_per_sec =
      static_cast<double>(token_bucket.tokens_per_fill().value()) / fill_interval_sec;

  uint64_t max_tokens = token_bucket.max_tokens();
  // Start the new token bucket with the same ratio of remaining tokens to max
  // tokens as the existing token bucket (best effort).
  uint64_t initial_tokens = (existing_token_bucket)
                                ? max_tokens * (existing_token_bucket->remainingTokens() /
                                                existing_token_bucket->maxTokens())
                                : max_tokens;
  return std::make_shared<AtomicTokenBucketImpl>(max_tokens, time_source, fill_rate_per_sec,
                                                 initial_tokens);
}

void GlobalRateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  if (response == nullptr) {
    return;
  }
  main_dispatcher_.post(
      [&, response = std::move(response)]() { onQuotaResponseImpl(response.get()); });
}

// Updating a cached_bucket shouldn't reset the cached token bucket if the
// existing token bucket rate_limit_strategy matches the new one.
bool shouldReplaceTokenBucket(const CachedBucket* cached_bucket,
                              const RateLimitStrategy& token_bucket_strategy) {
  return (!actionHasTokenBucket(cached_bucket->cached_action.get()) ||
          !protoTokenBucketsEq(token_bucket_strategy.token_bucket(),
                               cached_bucket->cached_action->quota_assignment_action()
                                   .rate_limit_strategy()
                                   .token_bucket()));
}

void GlobalRateLimitClientImpl::onQuotaResponseImpl(const RateLimitQuotaResponse* response) {
  ENVOY_LOG(debug, "The response that is received from RLQS server:\n{}", response->DebugString());
  for (const auto& action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error,
                "Received an RLQS response, but a bucket is missing its id. "
                "Complete response: {}",
                response->ShortDebugString());
      continue;
    }

    // Get the hash id value from BucketId in the response.
    const size_t bucket_id = MessageUtil::hash(action.bucket_id());
    auto bucket_it = buckets_cache_.find(bucket_id);
    if (bucket_it == buckets_cache_.end()) {
      // The response should be matched to the report we sent.
      ENVOY_LOG(error,
                "Received a response, but it includes an unexpected bucket "
                "that isn't present in the bucket cache. ID: {}. From response: {}",
                action.bucket_id().ShortDebugString(), response->ShortDebugString());
      continue;
    }
    // Indexed bucket in the source-of-truth cache. The indexed shared_ptr
    // should never be null. If it is null due to a bug, this will crash.
    std::shared_ptr<CachedBucket> cached_bucket = bucket_it->second;
    // Create a new bucket from the copy-able fields of the cached bucket.
    // Timers & old action are intentionally not carried over.
    std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
        /*bucket_id=*/cached_bucket->bucket_id,
        /*quota_usage=*/cached_bucket->quota_usage,
        /*cached_action=*/std::make_unique<BucketAction>(action),
        /*fallback_action=*/cached_bucket->fallback_action,
        /*fallback_ttl=*/cached_bucket->fallback_ttl,
        /*default_action=*/cached_bucket->default_action,
        /*token_bucket_limiter=*/nullptr);

    // Translate `quota_assignment_action` to a TokenBucket or a blanket
    // assignment as appropriate.
    if (action.has_quota_assignment_action()) {
      const auto& rate_limit_strategy = action.quota_assignment_action().rate_limit_strategy();
      switch (rate_limit_strategy.strategy_case()) {
      case RateLimitStrategy::kBlanketRule:
        // No additional processing needed.
        break;
      case RateLimitStrategy::kTokenBucket:
        // Only create a new TokenBucket if the configuration is new or
        // different from the cache.)
        if (shouldReplaceTokenBucket(cached_bucket.get(), rate_limit_strategy)) {
          bucket->token_bucket_limiter = createTokenBucketFromAction(
              rate_limit_strategy, time_source_, cached_bucket->token_bucket_limiter.get());
          ENVOY_LOG(info,
                    "A new TokenBucket has been configured by the RLQS "
                    "filter for id: {}",
                    bucket_id);
        } else {
          bucket->token_bucket_limiter = cached_bucket->token_bucket_limiter;
          ENVOY_LOG(info,
                    "The TokenBucket for id: {} is carried over during "
                    "response processing as the assignment hasn't changed.",
                    bucket_id);
        }
        break;
      case RateLimitStrategy::kRequestsPerTimeUnit:
        ENVOY_LOG(error, "RequestsPerTimeUnit rate limit strategies are not yet "
                         "supported in RLQS.");
        continue;
      case RateLimitStrategy::STRATEGY_NOT_SET:
        ENVOY_LOG(error, "Unexpected rate limit strategy in RLQS response: {}", bucket_id);
        continue;
      }
    } else if (action.has_abandon_action()) {
      buckets_cache_.erase(bucket_id);
      ENVOY_LOG(debug, "Cached bucket wiped by abandon action for bucket id: {}.", bucket_id);
      continue;
    }

    // Set the source of truth to the new bucket.
    buckets_cache_[bucket_id] = bucket;

    // Start the expiration timer for the newly set action based on the its TTL.
    startActionExpirationTimer(bucket.get(), bucket_id);
  }
  // Push updates to TLS.
  writeBucketsToTLS();
  if (callbacks_ != nullptr) {
    callbacks_->onQuotaResponseProcessed();
  }
}

void GlobalRateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                              const std::string& message) {
  // TODO(tyxia) Revisit later, maybe add some logging.
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // Stream is already closed and cannot be referenced further.
  ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status, message);
  stream_ = nullptr;
}

bool GlobalRateLimitClientImpl::startStreamImpl() {
  // Starts stream if it has not been opened yet.
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Trying to start the new gRPC stream");
    stream_ = async_client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                                       "envoy.service.rate_limit_quota.v3.RateLimitQuotaService."
                                       "StreamRateLimitQuotas"),
                                   *this, Http::AsyncClient::RequestOptions());
  }
  // Returns error status if start failed (i.e., stream_ is nullptr).
  return (stream_ != nullptr);
}

void GlobalRateLimitClientImpl::startSendReportsTimerImpl() {
  if (send_reports_timer_ != nullptr) {
    return;
  }
  ENVOY_LOG(debug, "Start the usage reporting timer for the RLQS stream.");
  send_reports_timer_ = main_dispatcher_.createTimer([&]() {
    onSendReportsTimer();
    if (callbacks_ != nullptr) {
      callbacks_->onUsageReportsSent();
    }
    send_reports_timer_->enableTimer(send_reports_interval_);
  });
  send_reports_timer_->enableTimer(send_reports_interval_);
}

void GlobalRateLimitClientImpl::onSendReportsTimer() {
  RateLimitQuotaUsageReports reports = buildReports();
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "The RLQS stream is not currently open. Attempting to start / "
                     "restart it now.");
    if (!startStreamImpl()) {
      ENVOY_LOG(error, "Failed to start the RLQS stream. Dropping the collected usage "
                       "reports.");
      return;
    }
  }
  sendUsageReportImpl(reports);
}

void GlobalRateLimitClientImpl::startActionExpirationTimer(CachedBucket* cached_bucket, size_t id) {
  // Pointer safety as all writes are against the source-of-truth.
  cached_bucket->action_expiration_timer = main_dispatcher_.createTimer([&, id, cached_bucket]() {
    onActionExpirationTimer(cached_bucket, id);
    if (callbacks_ != nullptr) {
      callbacks_->onActionExpiration();
    }
  });
  std::chrono::milliseconds ttl = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::seconds(cached_bucket->cached_action->quota_assignment_action()
                               .assignment_time_to_live()
                               .seconds()));
  cached_bucket->action_expiration_timer->enableTimer(ttl);
}

void GlobalRateLimitClientImpl::onActionExpirationTimer(CachedBucket* bucket, size_t id) {
  // Find index of the cached bucket in the source-of-truth.
  auto bucket_it = buckets_cache_.find(id);
  if (bucket_it == buckets_cache_.end() || bucket_it->second.get() != bucket) {
    // The bucket has been deleted while this was queued.
    return;
  }
  std::shared_ptr<CachedBucket> cached_bucket = bucket_it->second;

  // Without a fallback action, the cached action will be deleted and the bucket
  // will revert to its default action.
  if (cached_bucket->fallback_action == nullptr) {
    ENVOY_LOG(debug,
              "No fallback action is configured for bucket id {}, reverting to "
              "its default action.",
              id);
    buckets_cache_[id] = std::make_shared<CachedBucket>(
        /*bucket_id=*/cached_bucket->bucket_id,
        /*quota_usage=*/cached_bucket->quota_usage,
        /*cached_action=*/nullptr,
        /*fallback_action=*/cached_bucket->fallback_action,
        /*fallback_ttl=*/cached_bucket->fallback_ttl,
        /*default_action=*/cached_bucket->default_action,
        /*token_bucket_limiter=*/nullptr);
    // Write new bucket to TLS.
    writeBucketsToTLS();
    return;
  }

  ENVOY_LOG(debug, "The cached action for a bucket has expired, reverting to the "
                   "configured fallback action.");
  const auto& fallback_action = *cached_bucket->fallback_action;

  // Fallback to the configured fallback action.
  std::unique_ptr<BucketAction> new_action = std::make_unique<BucketAction>();
  new_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->MergeFrom(
      fallback_action);

  // Handle fallback to a TokenBucket if given.
  std::shared_ptr<AtomicTokenBucketImpl> new_token_bucket = nullptr;
  if (fallback_action.has_token_bucket() &&
      shouldReplaceTokenBucket(cached_bucket.get(), fallback_action)) {
    ENVOY_LOG(debug,
              "The cached token bucket at bucket id {} has been replaced by "
              "the configured fallback token bucket.",
              id);
    new_token_bucket = createTokenBucketFromAction(fallback_action, time_source_,
                                                   cached_bucket->token_bucket_limiter.get());
  } else if (fallback_action.has_token_bucket()) {
    ENVOY_LOG(debug,
              "The cached token bucket at bucket id {} is carrying over during "
              "fallback.",
              id);
    new_token_bucket = cached_bucket->token_bucket_limiter;
  } // else not a TokenBucket fallback so leave the new token bucket null.

  // Build the new cached bucket from the fallback action and new token bucket.
  std::shared_ptr<CachedBucket> new_bucket = std::make_shared<CachedBucket>(
      /*bucket_id=*/cached_bucket->bucket_id,
      /*quota_usage=*/cached_bucket->quota_usage,
      /*cached_action=*/std::move(new_action),
      /*fallback_action=*/cached_bucket->fallback_action,
      /*fallback_ttl=*/cached_bucket->fallback_ttl,
      /*default_action=*/cached_bucket->default_action,
      /*token_bucket_limiter=*/new_token_bucket);
  buckets_cache_[id] = new_bucket;
  // Start the fallback ttl timer for the new bucket.
  startFallbackExpirationTimer(new_bucket.get(), id);
  // Write new bucket to TLS.
  writeBucketsToTLS();
}

// Start a timer for the duration of the fallback action's TTL.
void GlobalRateLimitClientImpl::startFallbackExpirationTimer(CachedBucket* cached_bucket,
                                                             size_t id) {
  // Pointer safety as all writes are against the source-of-truth.
  cached_bucket->fallback_expiration_timer = main_dispatcher_.createTimer([&, id, cached_bucket]() {
    onFallbackExpirationTimer(cached_bucket, id);
    if (callbacks_ != nullptr) {
      callbacks_->onFallbackExpiration();
    }
  });
  cached_bucket->fallback_expiration_timer->enableTimer(cached_bucket->fallback_ttl);
}

void GlobalRateLimitClientImpl::onFallbackExpirationTimer(CachedBucket* bucket, size_t id) {
  // Find index of the cached bucket in the source-of-truth.
  auto bucket_it = buckets_cache_.find(id);
  if (bucket_it == buckets_cache_.end() || bucket_it->second.get() != bucket) {
    // The bucket has been deleted while this was queued.
    return;
  }
  std::shared_ptr<CachedBucket> cached_bucket = bucket_it->second;

  // Once the fallback action expires, the next step is to fallback to the
  // no_assignment_behavior, so do not set a cached action.
  ENVOY_LOG(debug, "The fallback action for a bucket has expired, reverting to "
                   "the default action.");
  buckets_cache_[id] = std::make_shared<CachedBucket>(
      /*bucket_id=*/cached_bucket->bucket_id,
      /*quota_usage=*/cached_bucket->quota_usage,
      /*cached_action=*/nullptr,
      /*fallback_action=*/cached_bucket->fallback_action,
      /*fallback_ttl=*/cached_bucket->fallback_ttl,
      /*default_action=*/cached_bucket->default_action,
      /*token_bucket_limiter=*/nullptr);
  // Write new bucket to TLS.
  writeBucketsToTLS();
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

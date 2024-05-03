#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"
#include "source/common/common/logger.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"
#include "google/protobuf/descriptor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

Grpc::RawAsyncClientSharedPtr getOrThrow(
    absl::StatusOr<Grpc::RawAsyncClientSharedPtr> client_or_error) {
  THROW_IF_STATUS_NOT_OK(client_or_error, throw);
  return client_or_error.value();
}

using BucketAction = RateLimitQuotaResponse::BucketAction;
using envoy::type::v3::RateLimitStrategy;

GlobalRateLimitClientImpl::GlobalRateLimitClientImpl(
    const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
    Server::Configuration::FactoryContext& context,
    absl::string_view domain_name,
    std::chrono::milliseconds send_reports_interval,
    Envoy::ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
    Envoy::Event::Dispatcher& main_dispatcher)
    : domain_name_(domain_name),
      aync_client_(
          getOrThrow(context.serverFactoryContext()
                         .clusterManager()
                         .grpcAsyncClientManager()
                         .getOrCreateRawAsyncClientWithHashKey(
                             config_with_hash_key, context.scope(), true))),
      buckets_tls_(buckets_tls),
      send_reports_interval_(send_reports_interval),
      time_source_(
          context.serverFactoryContext().mainThreadDispatcher().timeSource()),
      main_dispatcher_(main_dispatcher) {}

// Read all active buckets' aggregated usage & build them into a UsageReports
// message.
absl::StatusOr<RateLimitQuotaUsageReports>
GlobalRateLimitClientImpl::buildReports() {
  if (!buckets_tls_.get().has_value()) {
    return absl::InternalError(
        "Buckets cache is needed but not available for the active RLQS client "
        "to read & build usage reports.");
  }

  RateLimitQuotaUsageReports report;
  // Build the report from quota bucket source-of-truth. The buckets_cache_ is
  // guaranteed to be safe so long as index deletion & creation only happen in
  // the main thread.
  for (const auto& [_, cached] : buckets_cache_) {
    if (!cached) {
      ENVOY_LOG(error, "Bug: bucket cache is null for an in-use bucket.");
      continue;
    }

    std::shared_ptr<QuotaUsage> cached_usage = cached->quota_usage;
    if (!cached_usage) {
      ENVOY_LOG(error, "Bug: quota_usage cache is null for an in-use bucket.");
      continue;
    }
    auto* usage = report.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = cached->bucket_id;

    std::atomic<uint64_t>& num_requests_allowed =
        cached_usage->num_requests_allowed;
    std::atomic<uint64_t>& num_requests_denied =
        cached_usage->num_requests_denied;

    // Reset usage atomics to 0 and save current values as request totals.
    uint64_t allowed = num_requests_allowed.load(std::memory_order_relaxed);
    while (!num_requests_allowed.compare_exchange_weak(
        allowed, 0, std::memory_order_relaxed)) {
    }
    uint64_t denied = num_requests_denied.load(std::memory_order_relaxed);
    while (!num_requests_denied.compare_exchange_weak(
        denied, 0, std::memory_order_relaxed)) {
    }

    usage->set_num_requests_allowed(allowed);
    usage->set_num_requests_denied(denied);

    // Get the time elapsed since this bucket last went into a usage report.
    std::atomic<std::chrono::nanoseconds>& cached_last_report =
        cached_usage->last_report;
    std::chrono::nanoseconds now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            time_source_.monotonicTime().time_since_epoch());
    // `last_report` should always be set in the cached bucket.
    std::chrono::nanoseconds last_report =
        cached_last_report.load(std::memory_order_relaxed);
    // Update the last_report time point.
    while (!cached_last_report.compare_exchange_weak(
        last_report, now, std::memory_order_relaxed)) {
    }

    usage->mutable_time_elapsed()->set_seconds(
        std::chrono::duration_cast<std::chrono::seconds>(now - last_report)
            .count());
  }

  // Set the domain name.
  report.set_domain(domain_name_);
  ENVOY_LOG(debug, "The usage report that will be sent to RLQS server:\n{}",
            report.DebugString());
  return report;
}

void GlobalRateLimitClientImpl::createBucket(
    const BucketId& bucket_id, size_t id,
    const BucketAction& initial_bucket_action, bool initial_request_allowed) {
  main_dispatcher_.post(
      [&, bucket_id, id, initial_bucket_action, initial_request_allowed]() {
        createBucketImpl(bucket_id, id, initial_bucket_action,
                         initial_request_allowed);
      });
}

void GlobalRateLimitClientImpl::createBucketImpl(
    const BucketId& bucket_id, size_t id,
    const BucketAction& initial_bucket_action, bool initial_request_allowed) {
  // On the first createBucket call (so the first time a bucket is hit in the
  // RLQS filter), start the stream & reporting timer.
  if (!stream_tried_by_bucket_creation_) {
    stream_tried_by_bucket_creation_ = true;
    startSendReportsTimerImpl();
    if (startStreamImpl()) {
      ENVOY_LOG(info, "RLQS stream started successfully.");
    } else {
      ENVOY_LOG(error,
                "RLQS stream failed to start. Usage collection will continue "
                "regardless while reattempting to open the stream.");
    }
  }

  // If multiple createBucket calls were posted before the bucket was pushed to
  // TLS, just increment the appropriate usage counter.
  if (auto bucket_it = buckets_cache_.find(id);
      bucket_it != buckets_cache_.end()) {
    std::shared_ptr<CachedBucket> bucket = bucket_it->second;
    if (!bucket) {
      ENVOY_LOG(error,
                "Bug: bucket has been added into the source-of-truth with an "
                "invalid (null) cache ptr, ID: ",
                bucket_id.ShortDebugString());
      return;
    }

    std::shared_ptr<QuotaUsage> quota_usage = bucket->quota_usage;
    if (!quota_usage) {
      ENVOY_LOG(error,
                "Bug: quota_usage cache is null for a bucket that has already "
                "finished initialization in the source-of-truth, ID: ",
                bucket_id.ShortDebugString());
      return;
    }
    // Increment num_requests_(allowed|denied) based on the allow/deny choice
    // already made by the calling filter.
    std::atomic<uint64_t>& num_requests =
        (initial_request_allowed ? quota_usage->num_requests_allowed
                                 : quota_usage->num_requests_denied);
    uint64_t expected = num_requests.load(std::memory_order_relaxed);
    while (!num_requests.compare_exchange_weak(expected, expected + 1,
                                               std::memory_order_relaxed)) {
    }
    return;
  }

  // Create new bucket and add it into the source-of-truth then pointer-swap
  // with the BucketsCache already in TLS. Start the QuotaUsage at 1
  // request to count the request that was allowed/denied before calling to
  // CreateBucket. Default the bucket's action assignment to the configured
  // no_assignment_behavior.
  std::chrono::nanoseconds now =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          time_source_.monotonicTime().time_since_epoch());
  buckets_cache_[id] = std::make_shared<CachedBucket>(
      std::move(bucket_id),
      std::make_shared<QuotaUsage>(initial_request_allowed,
                                   !initial_request_allowed, now),
      initial_bucket_action, nullptr);
  writeBucketsToTLS();
  if (callbacks_)
    callbacks_->onBucketCreated(buckets_cache_[id]->bucket_id, id);
}

// This helper function reads from the current usage caches & sends the
// resulting reports message over the stream.
void GlobalRateLimitClientImpl::sendUsageReportImpl(
    const RateLimitQuotaUsageReports& reports) {
  if (stream_ == nullptr) {
    ENVOY_LOG(error,
              "Attempted to send a UsageReports message across the RLQS stream "
              "but it has already closed.");
    return;
  }
  stream_->sendMessage(reports, /*end_stream=*/false);
}

bool actionHasTokenBucket(const BucketAction& bucket_action) {
  return (bucket_action.has_quota_assignment_action() &&
          bucket_action.quota_assignment_action().has_rate_limit_strategy() &&
          bucket_action.quota_assignment_action()
              .rate_limit_strategy()
              .has_token_bucket());
}

// Check if TokenBuckets are deeply equal.
bool protoTokenBucketsEq(const ::envoy::type::v3::TokenBucket& new_tb,
                         const ::envoy::type::v3::TokenBucket& old_tb) {
  return (new_tb.max_tokens() == old_tb.max_tokens() &&
          new_tb.tokens_per_fill().value() ==
              old_tb.tokens_per_fill().value() &&
          new_tb.fill_interval().nanos() == old_tb.fill_interval().nanos());
}

// Check if RequestsPerTimeUnit are deeply equal.
bool requestsPerTimeUnitEq(
    const RateLimitStrategy::RequestsPerTimeUnit& old_rpu,
    const RateLimitStrategy::RequestsPerTimeUnit& new_rpu) {
  return (new_rpu.requests_per_time_unit() ==
              old_rpu.requests_per_time_unit() &&
          new_rpu.time_unit() == old_rpu.time_unit());
}

std::shared_ptr<::Envoy::TokenBucket> createTokenBucketFromAction(
    const RateLimitStrategy& strategy, TimeSource& time_source) {
  const auto& token_bucket = strategy.token_bucket();
  const auto& interval_proto = token_bucket.fill_interval();
  // Convert absl::duration to int64_t seconds
  int64_t fill_interval_sec =
      absl::ToInt64Seconds(absl::Seconds(interval_proto.seconds()) +
                           absl::Nanoseconds(interval_proto.nanos()));
  double fill_rate_per_sec =
      static_cast<double>(token_bucket.tokens_per_fill().value()) /
      fill_interval_sec;

  return std::make_shared<TokenBucketImpl>(token_bucket.max_tokens(),
                                           time_source, fill_rate_per_sec);
}

void GlobalRateLimitClientImpl::onReceiveMessage(
    RateLimitQuotaResponsePtr&& response) {
  if (!response) return;
  main_dispatcher_.post([&, response = std::move(response)]() {
    onQuotaResponseImpl(response.get());
  });
}

void GlobalRateLimitClientImpl::onQuotaResponseImpl(
    const RateLimitQuotaResponse* response) {
  ENVOY_LOG(debug, "The response that is received from RLQS server:\n{}",
            response->DebugString());
  for (const auto& action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error,
                "Received an RLQS response, but a bucket is missing its id. "
                "Complete response: ",
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
                "that isn't present in the bucket cache. ID: ",
                action.bucket_id().ShortDebugString(),
                ". From response: ", response->ShortDebugString());
      continue;
    }

    // Index in the source-of-truth BucketCache.
    std::shared_ptr<CachedBucket> bucket = bucket_it->second;
    if (!bucket) {
      ENVOY_LOG(error,
                "BUG: received a response with a bucket that is present in the "
                "cache but whose index holds only null. ID: ",
                action.bucket_id().ShortDebugString(),
                ". From response: ", response->ShortDebugString());
      continue;
    }

    // Translate `quota_assignment_action` to a TokenBucket or a blanket
    // assignment as appropriate.
    if (action.has_quota_assignment_action()) {
      const auto& rate_limit_strategy =
          action.quota_assignment_action().rate_limit_strategy();
      switch (rate_limit_strategy.strategy_case()) {
        case RateLimitStrategy::kBlanketRule:
          // No additional processing needed.
          break;
        case RateLimitStrategy::kTokenBucket:
          // Only create a new TokenBucket if the configuration is new or
          // different from the cache.)
          if (!actionHasTokenBucket(bucket->bucket_action) ||
              !protoTokenBucketsEq(
                  rate_limit_strategy.token_bucket(),
                  bucket->bucket_action.quota_assignment_action()
                      .rate_limit_strategy()
                      .token_bucket())) {
            bucket->token_bucket_limiter =
                createTokenBucketFromAction(rate_limit_strategy, time_source_);

            ENVOY_LOG(info,
                      "A new TokenBucket has been configured by the RLQS "
                      "filter for id: ",
                      action.bucket_id().ShortDebugString());
          }
          break;
        case RateLimitStrategy::kRequestsPerTimeUnit:
          ENVOY_LOG(error,
                    "RequestsPerTimeUnit rate limit strategies is not yet "
                    "supported in RLQS.");
          break;
        default:
          ENVOY_LOG(error, "Unexpected rate limit strategy in RLQS response: ",
                    rate_limit_strategy.ShortDebugString());
      }
    } else if (action.has_abandon_action()) {
      ENVOY_LOG(debug, "Abandon action is not yet handled properly in RLQS.");
    }
    bucket->bucket_action = action;
  }
  // Push updates to TLS.
  writeBucketsToTLS();
  if (callbacks_) callbacks_->onQuotaResponseProcessed();
}

void GlobalRateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                              const std::string& message) {
  // TODO(tyxia) Revisit later, maybe add some logging.
  main_dispatcher_.post([&, status, message]() {
    // Stream is already closed and cannot be referenced further.
    ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status,
              message);
    stream_closed_ = true;
    stream_ = nullptr;
  });
}

bool GlobalRateLimitClientImpl::startStreamImpl() {
  // Starts stream if it has not been opened yet.
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Trying to start the new gRPC stream");
    stream_ = aync_client_.start(
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.rate_limit_quota.v3.RateLimitQuotaService."
            "StreamRateLimitQuotas"),
        *this, Http::AsyncClient::RequestOptions());
  }
  stream_closed_ = (stream_ == nullptr);
  // Returns error status if start failed (i.e., stream_ is nullptr).
  return (stream_ != nullptr);
}

void GlobalRateLimitClientImpl::startSendReportsTimerImpl() {
  if (!send_reports_timer_) {
    ENVOY_LOG(debug, "Start the usage reporting timer for the RLQS stream.");
    send_reports_timer_ = main_dispatcher_.createTimer([&]() {
      main_dispatcher_.post([&]() {
        onSendReportsTimer();
        if (callbacks_) callbacks_->onUsageReportsSent();
        send_reports_timer_->enableTimer(send_reports_interval_);
      });
    });
    send_reports_timer_->enableTimer(send_reports_interval_);
  }
}

void GlobalRateLimitClientImpl::onSendReportsTimer() {
  absl::StatusOr<RateLimitQuotaUsageReports> reports = buildReports();
  if (!reports.ok()) {
    ENVOY_LOG(error, "Failed to build a RLQS Usage Reports message: ",
              reports.status().message());
    return;
  }
  if (stream_closed_) {
    ENVOY_LOG(debug,
              "The RLQS stream is not currently open. Attempting to start / "
              "restart it now.");
    if (!startStreamImpl()) {
      ENVOY_LOG(error,
                "Failed to start the RLQS stream. Dropping the collected usage "
                "reports.");
      return;
    }
  }
  sendUsageReportImpl(*reports);
}

}  // namespace RateLimitQuota
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy

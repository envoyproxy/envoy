#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

const char kBucketMetadataNamespace[] = "envoy.extensions.http_filters.rate_limit_quota.bucket";

using envoy::type::v3::RateLimitStrategy;
using NoAssignmentBehavior = envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings::NoAssignmentBehavior;

// Returns whether or not to allow a request based on the no-assignment-behavior
// & populates an action.
bool noAssignmentBehaviorShouldAllow(const NoAssignmentBehavior& no_assignment_behavior) {
  // Only a blanket DENY_ALL fall-back should block the very first request.
  return !(no_assignment_behavior.fallback_rate_limit().has_blanket_rule() &&
           no_assignment_behavior.fallback_rate_limit().blanket_rule() ==
               RateLimitStrategy::DENY_ALL);
}

Http::FilterHeadersStatus sendDenyResponse(Http::StreamDecoderFilterCallbacks* cb,
                                           Envoy::Http::Code code,
                                           StreamInfo::CoreResponseFlag flag) {
  cb->sendLocalReply(code, "", nullptr, absl::nullopt, "");
  cb->streamInfo().setResponseFlag(flag);
  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  // First, perform the request matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);
  if (!match_result.ok()) {
    // When the request is not matched by any matchers, it is ALLOWED by default
    // (i.e., fail-open) and its quota usage will not be reported to RLQS
    // server.
    // TODO(tyxia) Add stats here and other places throughout the filter. e.g.
    // request allowed/denied, matching succeed/fail and so on.
    ENVOY_LOG(debug,
              "The request is not matched by any matchers: ", match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Second, generate the bucket id for this request based on match action when
  // the request matching succeeds.
  const RateLimitOnMatchAction& match_action =
      match_result.value()->getTyped<RateLimitOnMatchAction>();
  absl::StatusOr<BucketId> ret =
      match_action.generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request, the
    // request is ALLOWED by default (i.e., fail-open).
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  const BucketId& bucket_id_proto = *ret;
  const size_t bucket_id = MessageUtil::hash(bucket_id_proto);
  ENVOY_LOG(trace, "Generated the associated hashed bucket id: {} for bucket id proto:\n {}",
            bucket_id, bucket_id_proto.DebugString());

  // Add the matched bucket_id to dynamic metadata for logging.
  ProtobufWkt::Struct bucket_log;
  auto* bucket_log_fields = bucket_log.mutable_fields();
  for (const auto& bucket : bucket_id_proto.bucket()) {
    (*bucket_log_fields)[bucket.first] = ValueUtil::stringValue(bucket.second);
  }
  callbacks_->streamInfo().setDynamicMetadata(kBucketMetadataNamespace, bucket_log);

  std::shared_ptr<CachedBucket> cached_bucket = client_->getBucket(bucket_id);
  if (cached_bucket != nullptr) {
    // Found the cached bucket entry.
    return processCachedBucket(*cached_bucket);
  }

  // New buckets should have a configured default action pulled from
  // no_assignment_behavior or a default ALLOW_ALL if unset.
  bool shouldAllowInitialRequest = true;
  BucketAction default_bucket_action;
  *default_bucket_action.mutable_bucket_id() = bucket_id_proto;
  if (match_action.bucketSettings().has_no_assignment_behavior()) {
    *default_bucket_action.mutable_quota_assignment_action()->mutable_rate_limit_strategy() =
        match_action.bucketSettings().no_assignment_behavior().fallback_rate_limit();
    shouldAllowInitialRequest =
        noAssignmentBehaviorShouldAllow(match_action.bucketSettings().no_assignment_behavior());
  } else {
    default_bucket_action.mutable_quota_assignment_action()
        ->mutable_rate_limit_strategy()
        ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  }

  // Determine expiration fallback behavior & TTL from
  // expired_assignment_behavior before resorting back to
  // no_assignment_behavior for stale buckets.
  std::unique_ptr<RateLimitStrategy> expiration_fallback_action =
      (match_action.bucketSettings().has_expired_assignment_behavior() &&
       match_action.bucketSettings().expired_assignment_behavior().has_fallback_rate_limit())
          ? std::make_unique<RateLimitStrategy>(
                match_action.bucketSettings().expired_assignment_behavior().fallback_rate_limit())
          : nullptr;
  uint64_t expiration_fallback_ttl_secs =
      (match_action.bucketSettings().has_expired_assignment_behavior() &&
       match_action.bucketSettings()
           .expired_assignment_behavior()
           .has_expired_assignment_behavior_timeout())
          ? match_action.bucketSettings()
                .expired_assignment_behavior()
                .expired_assignment_behavior_timeout()
                .seconds()
          : 0;
  std::chrono::milliseconds expiration_fallback_ttl =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::seconds(expiration_fallback_ttl_secs));

  // When seeing a new bucket for the first time, request its addition to
  // the global cache. This will be done by the main thread.
  client_->createBucket(bucket_id_proto, bucket_id, default_bucket_action,
                        std::move(expiration_fallback_action), expiration_fallback_ttl,
                        shouldAllowInitialRequest);
  ENVOY_LOG(debug, "Requesting addition to the global RLQS bucket cache: ",
            bucket_id_proto.ShortDebugString());

  if (shouldAllowInitialRequest) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  return sendDenyResponse(callbacks_, Envoy::Http::Code::TooManyRequests,
                          StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
}

// TODO(tyxia) Currently request matching is only performed on the request
// header.
absl::StatusOr<Matcher::ActionPtr>
RateLimitQuotaFilter::requestMatching(const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent
  // requests. This avoids creating the data object for every request, which
  // is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ == nullptr) {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
    data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(callbacks_->streamInfo());
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher tree has not been initialized yet.");
  }
  // Populate the request header.
  if (!headers.empty()) {
    data_ptr_->onRequestHeaders(headers);
  }

  // Perform the matching.
  auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);
  if (match_result.match_state_ != Matcher::MatchState::MatchComplete) {
    // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
    return absl::InternalError("Unable to match due to the required data not being available.");
  }
  if (!match_result.result_) {
    return absl::NotFoundError("Matching completed but no match result was found.");
  }
  // Return the matched result for `on_match` case.
  return match_result.result_();
}

void RateLimitQuotaFilter::onDestroy() {
  // TODO(tyxia) TLS resource are not cleaned here.
}

inline void incrementAtomic(std::atomic<uint64_t>& counter) {
  uint64_t current = counter.load(std::memory_order_relaxed);
  while (!counter.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
  }
}

bool RateLimitQuotaFilter::shouldAllowRequest(const CachedBucket& cached_bucket) {
  const BucketAction& bucket_action =
      (cached_bucket.cached_action) ? *cached_bucket.cached_action : cached_bucket.default_action;

  RateLimitStrategy rate_limit_strategy =
      (bucket_action.has_quota_assignment_action())
          ? bucket_action.quota_assignment_action().rate_limit_strategy()
          : RateLimitStrategy();

  switch (rate_limit_strategy.strategy_case()) {
  case RateLimitStrategy::kBlanketRule:
    switch (rate_limit_strategy.blanket_rule()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case RateLimitStrategy::ALLOW_ALL:
      return true;
    case RateLimitStrategy::DENY_ALL:
      return false;
    }
    break;
  case RateLimitStrategy::kTokenBucket:
    // A TokenBucket assignment should always have its accompanying
    // TokenBucket implementation in the cache. If it's null instead, then
    // it's due to a bug, and this will crash.
    return cached_bucket.token_bucket_limiter->consume(1, false);
  case RateLimitStrategy::kRequestsPerTimeUnit:
    // TODO(tyxia) Implement RequestsPerTimeUnit.
    ENVOY_LOG(warn, "RequestsPerTimeUnit is not yet supported by RLQS.");
    return true;
  case RateLimitStrategy::STRATEGY_NOT_SET:
    ENVOY_LOG(error, "Bug: an RLQS bucket is cached with a missing "
                     "quota_assignment_action or rate_limit_strategy causing the "
                     "filter to fail open.");
    return true;
  }
  return true; // Unreachable.
}

Http::FilterHeadersStatus RateLimitQuotaFilter::processCachedBucket(CachedBucket& cached_bucket) {
  // The QuotaUsage of a cached bucket should never be null. If it is due to a
  // bug, this will crash.
  std::shared_ptr<QuotaUsage> quota_usage = cached_bucket.quota_usage;

  if (shouldAllowRequest(cached_bucket)) {
    incrementAtomic(quota_usage->num_requests_allowed);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  incrementAtomic(quota_usage->num_requests_denied);
  // TODO(tyxia) Build the customized response based on
  // `DenyResponseSettings` if it is configured.
  return sendDenyResponse(callbacks_, Envoy::Http::Code::TooManyRequests,
                          StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

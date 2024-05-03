#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/stream_info.h"
#include "source/common/common/logger.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using envoy::type::v3::RateLimitStrategy;
using NoAssignmentBehavior = envoy::extensions::filters::http::
    rate_limit_quota::v3::RateLimitQuotaBucketSettings::NoAssignmentBehavior;

// Returns whether or not to allow a request based on the no-assignment-behavior
// & populates an action.
bool noAssignmentBehaviorShouldAllow(
    const NoAssignmentBehavior& no_assignment_behavior) {
  // Only a blanket DENY_ALL fall-back should block the very first request.
  return !(no_assignment_behavior.fallback_rate_limit().has_blanket_rule() &&
           no_assignment_behavior.fallback_rate_limit().blanket_rule() ==
               RateLimitStrategy::DENY_ALL);
}

Http::FilterHeadersStatus sendDenyResponse(
    Http::StreamDecoderFilterCallbacks* cb, Envoy::Http::Code code,
    StreamInfo::CoreResponseFlag flag) {
  cb->sendLocalReply(code, "", nullptr, absl::nullopt, "");
  cb->streamInfo().setResponseFlag(flag);
  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(
    Http::RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  // First, perform the request matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);
  if (!match_result.ok()) {
    // When the request is not matched by any matchers, it is ALLOWED by default
    // (i.e., fail-open) and its quota usage will not be reported to RLQS
    // server.
    // TODO(tyxia) Add stats here and other places throughout the filter. e.g.
    // request allowed/denied, matching succeed/fail and so on.
    ENVOY_LOG(debug, "The request is not matched by any matchers: ",
              match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Second, generate the bucket id for this request based on match action when
  // the request matching succeeds.
  const RateLimitOnMatchAction& match_action =
      match_result.value()->getTyped<RateLimitOnMatchAction>();
  auto ret =
      match_action.generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request, the
    // request is ALLOWED by default (i.e., fail-open).
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}",
              ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id_proto = ret.value();
  const size_t bucket_id = MessageUtil::hash(bucket_id_proto);
  ENVOY_LOG(
      trace,
      "Generated the associated hashed bucket id: {} for bucket id proto:\n {}",
      bucket_id, bucket_id_proto.DebugString());
  std::shared_ptr<CachedBucket> cached_bucket = client_->getBucket(bucket_id);
  if (cached_bucket) {
    // Found the cached bucket entry.
    return processCachedBucket(*cached_bucket);
  }

  // New buckets should initialize based on the configured strategy or a
  // default ALLOW_ALL.
  bool shouldAllowInitialRequest = true;
  BucketAction initial_bucket_action;
  *initial_bucket_action.mutable_bucket_id() = bucket_id_proto;
  if (match_action.bucketSettings().has_no_assignment_behavior()) {
    *initial_bucket_action.mutable_quota_assignment_action()
         ->mutable_rate_limit_strategy() = match_action.bucketSettings()
                                               .no_assignment_behavior()
                                               .fallback_rate_limit();
    shouldAllowInitialRequest = noAssignmentBehaviorShouldAllow(
        match_action.bucketSettings().no_assignment_behavior());
  } else {
    initial_bucket_action.mutable_quota_assignment_action()
        ->mutable_rate_limit_strategy()
        ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  }

  // When seeing a new bucket for the first time, request its addition to
  // the global cache. This will be done by the main thread.
  client_->createBucket(bucket_id_proto, bucket_id, initial_bucket_action,
                        shouldAllowInitialRequest);
  ENVOY_LOG(debug, "Requesting addition to the global RLQS bucket cache: ",
            bucket_id_proto.ShortDebugString());

  if (shouldAllowInitialRequest) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  return sendDenyResponse(
      callbacks_, Envoy::Http::Code::TooManyRequests,
      StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
  ;
}

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMatchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData,
                            RateLimitOnMatchActionContext>
      factory(context, factory_context_.serverFactoryContext(), visitor_);
  if (config_->has_bucket_matchers()) {
    matcher_ = factory.create(config_->bucket_matchers())();
  }
}

// TODO(tyxia) Currently request matching is only performed on the request
// header.
absl::StatusOr<Matcher::ActionPtr> RateLimitQuotaFilter::requestMatching(
    const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent
  // requests. This avoids creating the data object for every request, which
  // is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo());
    } else {
      return absl::InternalError(
          "Filter callback has not been initialized successfully yet.");
    }
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher tree has not been initialized yet.");
  } else {
    // Populate the request header.
    if (!headers.empty()) {
      data_ptr_->onRequestHeaders(headers);
    }

    // Perform the matching.
    auto match_result =
        Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // Return the matched result for `on_match` case.
        return match_result.result_();
      } else {
        return absl::NotFoundError(
            "Matching completed but no match result was found.");
      }
    } else {
      // The returned state from `evaluateMatch` function is
      // `MatchState::UnableToMatch` here.
      return absl::InternalError(
          "Unable to match due to the required data not being available.");
    }
  }
}

void RateLimitQuotaFilter::onDestroy() {
  // TODO(tyxia) TLS resource are not cleaned here.
}

inline void incrementAtomic(std::atomic<uint64_t>& counter) {
  uint64_t current = counter.load(std::memory_order_relaxed);
  while (!counter.compare_exchange_weak(current, current + 1,
                                        std::memory_order_relaxed)) {
  }
}

bool RateLimitQuotaFilter::shouldAllowRequest(
    const CachedBucket& cached_bucket) {
  RateLimitStrategy rate_limit_strategy =
      (cached_bucket.bucket_action.has_quota_assignment_action())
          ? cached_bucket.bucket_action.quota_assignment_action()
                .rate_limit_strategy()
          : RateLimitStrategy();

  switch (rate_limit_strategy.strategy_case()) {
    case RateLimitStrategy::kBlanketRule:
      switch (rate_limit_strategy.blanket_rule()) {
        case RateLimitStrategy::ALLOW_ALL:
          return true;
        case RateLimitStrategy::DENY_ALL:
          return false;
        default:
          ENVOY_LOG(error,
                    "Bug: an RLQS bucket is cached with an unexpected type of "
                    "blanket rule causing the filter to fail open. ID: ",
                    cached_bucket.bucket_id.ShortDebugString(),
                    ". Strategy: ", rate_limit_strategy.ShortDebugString());
          return true;
      }
      break;
    case RateLimitStrategy::kTokenBucket:
      if (!cached_bucket.token_bucket_limiter) {
        ENVOY_LOG(
            error,
            "Bug: an RLQS bucket is assigned a TokenBucket rate limiting "
            "strategy but no associated TokenBucket has been initialized.");
        return true;
      }
      return cached_bucket.token_bucket_limiter->consume(1, false);
    case RateLimitStrategy::kRequestsPerTimeUnit:
      // TODO(tyxia) Implement RequestsPerTimeUnit.
      ENVOY_LOG(warn, "RequestsPerTimeUnit is not yet supported by RLQS.");
      return true;
    case RateLimitStrategy::STRATEGY_NOT_SET:
      ENVOY_LOG(error,
                "Bug: an RLQS bucket is cached with a missing "
                "quota_assignment_action or rate_limit_strategy causing the "
                "filter to fail open.");
      return true;
  }
}

Http::FilterHeadersStatus RateLimitQuotaFilter::processCachedBucket(
    CachedBucket& cached_bucket) {
  std::shared_ptr<QuotaUsage> quota_usage = cached_bucket.quota_usage;
  if (!quota_usage) {
    ENVOY_LOG(error,
              "Bug: RLQS filter unable to increment usage counters for a "
              "cached bucket as the pointer to the usage cache is null. This "
              "should never be the case.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  if (shouldAllowRequest(cached_bucket)) {
    incrementAtomic(quota_usage->num_requests_allowed);
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  incrementAtomic(quota_usage->num_requests_denied);
  // TODO(tyxia) Build the customized response based on
  // `DenyResponseSettings` if it is configured.
  return sendDenyResponse(
      callbacks_, Envoy::Http::Code::TooManyRequests,
      StreamInfo::CoreResponseFlag::ResponseFromCacheFilter);
}

}  // namespace RateLimitQuota
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy

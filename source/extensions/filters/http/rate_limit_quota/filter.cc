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
#include "envoy/type/v3/http_status.pb.h"
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
const char kPreviewBucketMetadataNamespace[] =
    "envoy.extensions.http_filters.rate_limit_quota.preview_bucket";

using envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;
using envoy::type::v3::RateLimitStrategy;
using NoAssignmentBehavior = RateLimitQuotaBucketSettings::NoAssignmentBehavior;
using DenyResponseSettings = RateLimitQuotaBucketSettings::DenyResponseSettings;
using MatchTree = Matcher::MatchTree<Http::HttpMatchingData>;
using MatchTreePtr = std::unique_ptr<MatchTree>;

// Returns whether or not to allow a request based on the no-assignment-behavior
// & populates an action.
bool noAssignmentBehaviorShouldAllow(const NoAssignmentBehavior& no_assignment_behavior) {
  // Only a blanket DENY_ALL fall-back should block the very first request.
  return !(no_assignment_behavior.fallback_rate_limit().has_blanket_rule() &&
           no_assignment_behavior.fallback_rate_limit().blanket_rule() ==
               RateLimitStrategy::DENY_ALL);
}

// Translate from the HttpStatus Code enum to the Envoy::Http::Code enum.
inline Envoy::Http::Code getDenyResponseCode(const DenyResponseSettings& settings) {
  if (!settings.has_http_status()) {
    return Envoy::Http::Code::TooManyRequests;
  }
  return static_cast<Envoy::Http::Code>(static_cast<uint64_t>(settings.http_status().code()));
}

inline std::function<void(Http::ResponseHeaderMap&)>
addDenyResponseHeadersCb(const DenyResponseSettings& settings) {
  if (settings.response_headers_to_add().empty())
    return nullptr;
  // Headers copied from settings for thread-safety.
  return [headers_to_add = settings.response_headers_to_add()](Http::ResponseHeaderMap& headers) {
    for (const envoy::config::core::v3::HeaderValueOption& header : headers_to_add) {
      headers.addCopy(Http::LowerCaseString(header.header().key()), header.header().value());
    }
  };
}

Http::FilterHeadersStatus sendDenyResponse(Http::StreamDecoderFilterCallbacks* cb,
                                           const DenyResponseSettings& settings,
                                           StreamInfo::CoreResponseFlag flag, bool preview) {
  if (!preview) {
    // Only set deny response details from non-preview actions.
    cb->sendLocalReply(getDenyResponseCode(settings), settings.http_body().value(),
                       addDenyResponseHeadersCb(settings), absl::nullopt, "");
    cb->streamInfo().setResponseFlag(flag);
  }
  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus
RateLimitQuotaFilter::executeMatchedAction(const RateLimitOnMatchAction& match_action,
                                           bool preview) {
  // Generate the bucket id for this request based on matched action.
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
  callbacks_->streamInfo().setDynamicMetadata(
      (preview) ? kPreviewBucketMetadataNamespace : kBucketMetadataNamespace, bucket_log);

  // Settings needed if a cached bucket or default behavior decides to deny.
  const DenyResponseSettings& deny_response_settings =
      match_action.bucketSettings().deny_response_settings();

  std::shared_ptr<CachedBucket> cached_bucket = client_->getBucket(bucket_id);
  if (cached_bucket != nullptr) {
    // Found the cached bucket entry.
    return processCachedBucket(deny_response_settings, *cached_bucket, preview);
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
  ENVOY_LOG(debug, "Requesting addition to the global RLQS bucket cache: {}",
            bucket_id_proto.ShortDebugString());

  if (shouldAllowInitialRequest) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  return sendDenyResponse(callbacks_, deny_response_settings,
                          StreamInfo::CoreResponseFlag::ResponseFromCacheFilter, preview);
}

// Search through re-entry points until the request matches a non-preview action.
absl::optional<RateLimitOnMatchAction> RateLimitQuotaFilter::findNonPreviewAction(
    Matcher::ReenterableMatchEvaluator<Http::HttpMatchingData>& reenterable_matcher,
    const Http::RequestHeaderMap& headers) {
  // Loop until we find a non-preview action, matching fails or we run out of re-entry points.
  while (true) {
    // Re-enter the MatcherTree to find the next action.
    auto reentry_match_result =
        requestMatching(reenterable_matcher, headers); // Exit if re-entry failed internally.
    if (!reentry_match_result.ok()) {
      ENVOY_LOG(error, "findNonPreviewAction: matching failed during re-entry: {}",
                reentry_match_result.status().message());
      break;
    }
    if (*reentry_match_result == nullptr) {
      ENVOY_LOG(debug, "findNonPreviewAction: no match found during re-entry.");
      break;
    }
    RateLimitOnMatchAction next_match_action =
        reentry_match_result.value()->getTyped<RateLimitOnMatchAction>();
    // Skip any matched, preview actions after the first.
    if (next_match_action.bucketSettings().preview_mode()) {
      ENVOY_LOG(
          debug,
          "findNonPreviewAction: skipped subsequent preview action during MatchTree re-entry.");
      continue;
    }
    ENVOY_LOG(debug, "findNonPreviewAction: found non-preview action during MatchTree re-entry.");
    return std::move(next_match_action);
  }
  return absl::nullopt;
}

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);

  // First, perform the request matching.
  if (matcher_ == nullptr) {
    ENVOY_LOG(error, "decodeHeaders: matcher tree is null.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  Matcher::ReenterableMatchEvaluator<Http::HttpMatchingData> reenterable_matcher(matcher_);
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(reenterable_matcher, headers);
  if (!match_result.ok()) {
    ENVOY_LOG(error, "decodeHeaders: the request failed open during matching: {}",
              match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  if (*match_result == nullptr) {
    // When the request is not matched by any matchers, it is ALLOWED by default
    // (i.e., fail-open) and its quota usage will not be reported to RLQS
    // server.
    // TODO(tyxia) Add stats here and other places throughout the filter. e.g.
    // request allowed/denied, matching succeed/fail and so on.
    ENVOY_LOG(debug, "decodeHeaders: the request defaulted open as it was not matched to any "
                     "action and did not have a set on_no_match action.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  RateLimitOnMatchAction match_action = match_result.value()->getTyped<RateLimitOnMatchAction>();
  // The simplest case is when the initially matched action is non-preview & enforceable.
  if (!match_action.bucketSettings().preview_mode()) {
    return executeMatchedAction(match_action, false);
  }
  // Else we execute standard usage aggregation, bucket creation, etc for the preview action but
  // ignore the resulting rate-limiting decision.
  executeMatchedAction(match_action, true);

  // Next, re-enter the MatcherTree, if possible, to find an enforceable action / on-no-match
  // action.
  absl::optional<RateLimitOnMatchAction> non_preview_action =
      findNonPreviewAction(reenterable_matcher, headers);
  if (!non_preview_action.has_value()) {
    ENVOY_LOG(debug, "decodeHeaders: the request is not matched by any non-preview matchers: ",
              match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  return executeMatchedAction(*non_preview_action, false);
}

// TODO(tyxia) Currently request matching is only performed on the request
// header.
absl::StatusOr<Matcher::ActionPtr> RateLimitQuotaFilter::requestMatching(
    Matcher::ReenterableMatchEvaluator<Http::HttpMatchingData>& reenterable_matcher,
    const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent
  // requests. This avoids creating the data object for every request, which
  // is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ == nullptr) {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
    data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(callbacks_->streamInfo());
  }

  // Populate the request header.
  if (!headers.empty()) {
    data_ptr_->onRequestHeaders(headers);
  }

  // Perform the matching.
  auto match_result = reenterable_matcher.evaluateMatch(*data_ptr_);
  if (match_result.match_state_ != Matcher::MatchState::MatchComplete) {
    // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
    return absl::InternalError("Unable to match due to the required data not being available.");
  }
  if (!match_result.result_) {
    return nullptr;
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

Http::FilterHeadersStatus
RateLimitQuotaFilter::processCachedBucket(const DenyResponseSettings& deny_response_settings,
                                          CachedBucket& cached_bucket, bool preview) {
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
  return sendDenyResponse(callbacks_, deny_response_settings,
                          StreamInfo::CoreResponseFlag::ResponseFromCacheFilter, preview);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

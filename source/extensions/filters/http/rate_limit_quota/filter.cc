#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

const char kBucketMetadataNamespace[] = "envoy.extensions.http_filters.rate_limit_quota.bucket";

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  // First, perform the request matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);
  if (!match_result.ok()) {
    // When the request is not matched by any matchers, it is ALLOWED by default (i.e., fail-open)
    // and its quota usage will not be reported to RLQS server.
    // TODO(tyxia) Add stats here and other places throughout the filter. e.g. request
    // allowed/denied, matching succeed/fail and so on.
    ENVOY_LOG(debug,
              "The request is not matched by any matchers: ", match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Second, generate the bucket id for this request based on match action when the request matching
  // succeeds.
  const RateLimitOnMatchAction& match_action =
      match_result.value()->getTyped<RateLimitOnMatchAction>();
  absl::StatusOr ret = match_action.generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request, the request is ALLOWED by
    // default (i.e., fail-open).
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  const BucketId& bucket_id_proto = *ret;
  const size_t bucket_id = MessageUtil::hash(bucket_id_proto);
  ENVOY_LOG(trace, "Generated the associated hashed bucket id: {} for bucket id proto:\n {}",
            bucket_id, bucket_id_proto.DebugString());

  ProtobufWkt::Struct bucket_log;
  auto* bucket_log_fields = bucket_log.mutable_fields();
  for (const auto& bucket : bucket_id_proto.bucket())
    (*bucket_log_fields)[bucket.first] = ValueUtil::stringValue(bucket.second);

  callbacks_->streamInfo().setDynamicMetadata(kBucketMetadataNamespace, bucket_log);

  if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
    // For first matched request, create a new bucket in the cache and sent the report to RLQS
    // server immediately.
    createNewBucket(bucket_id_proto, match_action, bucket_id);
    return sendImmediateReport(bucket_id, match_action);
  }

  return processCachedBucket(bucket_id, match_action);
}

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMatchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext> factory(
      context, factory_context_.serverFactoryContext(), visitor_);
  if (config_->has_bucket_matchers()) {
    matcher_ = factory.create(config_->bucket_matchers())();
  }
}

// TODO(tyxia) Currently request matching is only performed on the request header.
absl::StatusOr<Matcher::ActionPtr>
RateLimitQuotaFilter::requestMatching(const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent requests.
  // This avoids creating the data object for every request, which is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(callbacks_->streamInfo());
    } else {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
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
    auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // Return the matched result for `on_match` case.
        return match_result.result_();
      } else {
        return absl::NotFoundError("Matching completed but no match result was found.");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match due to the required data not being available.");
    }
  }
}

void RateLimitQuotaFilter::onQuotaResponse(RateLimitQuotaResponse&) {
  // RLQS filter is currently operating in non-blocking fashion. Thus, there is no need to continue
  // filter chain iteration.
  // Placeholder for other post processing if needed.
}

void RateLimitQuotaFilter::onDestroy() {
  // TODO(tyxia) TLS resource are not cleaned here.
}

void RateLimitQuotaFilter::createNewBucket(const BucketId& bucket_id,
                                           const RateLimitOnMatchAction& match_action, size_t id) {
  // Create new bucket and store it into quota cache.
  std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();

  // The first matched request doesn't have quota assignment from the RLQS server yet, so the
  // action is performed based on pre-configured strategy from no assignment behavior config.
  auto mutable_rate_limit_strategy =
      new_bucket->bucket_action.mutable_quota_assignment_action()->mutable_rate_limit_strategy();
  if (match_action.bucketSettings().has_no_assignment_behavior()) {
    *mutable_rate_limit_strategy =
        match_action.bucketSettings().no_assignment_behavior().fallback_rate_limit();
  } else {
    // When `no_assignment_behavior` is not configured, default blanket rule is set to ALLOW_ALL.
    // (i.e., fail-open).
    mutable_rate_limit_strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
  }

  // Set up the bucket id.
  new_bucket->bucket_id = bucket_id;
  // Set up the first time assignment time.
  new_bucket->first_assignment_time = time_source_.monotonicTime();
  // Set up the quota usage.
  QuotaUsage quota_usage;
  quota_usage.last_report = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source_.monotonicTime().time_since_epoch());

  switch (mutable_rate_limit_strategy->blanket_rule()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::type::v3::RateLimitStrategy::ALLOW_ALL:
    quota_usage.num_requests_allowed++;
    break;
  case envoy::type::v3::RateLimitStrategy::DENY_ALL:
    quota_usage.num_requests_denied++;
    break;
  }

  new_bucket->quota_usage = quota_usage;
  quota_buckets_[id] = std::move(new_bucket);
}

Http::FilterHeadersStatus
RateLimitQuotaFilter::sendImmediateReport(const size_t bucket_id,
                                          const RateLimitOnMatchAction& match_action) {
  const auto& bucket_settings = match_action.bucketSettings();

  // Create the gRPC client if it has not been created.
  if (client_.rate_limit_client == nullptr) {
    client_.rate_limit_client = createRateLimitClient(factory_context_, this, quota_buckets_,
                                                      config_->domain(), config_with_hash_key_);
  } else {
    // Callback has been reset to nullptr when filter was destroyed last time.
    // Reset it here when new filter has been created.
    client_.rate_limit_client->setCallback(this);
  }

  // It can not be nullptr based on current implementation.
  ASSERT(client_.rate_limit_client != nullptr);

  // Start the streaming on the first request.
  // It will be a no-op if the stream is already active.
  auto status = client_.rate_limit_client->startStream(callbacks_->streamInfo());
  if (!status.ok()) {
    ENVOY_LOG(error, "Failed to start the gRPC stream: ", status.message());
    // TODO(tyxia) Check `NoAssignmentBehavior` behavior instead of fail-open here.
    return Envoy::Http::FilterHeadersStatus::Continue;
  } else {
    ENVOY_LOG(debug, "The gRPC stream is established and active");
  }

  // Send the usage report to RLQS server immediately on the first time when the request is
  // matched.
  client_.rate_limit_client->sendUsageReport(bucket_id);

  ASSERT(client_.send_reports_timer != nullptr);
  // Set the reporting interval and enable the timer.
  const int64_t reporting_interval = PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
  client_.report_interval_ms = std::chrono::milliseconds(reporting_interval);
  client_.send_reports_timer->enableTimer(client_.report_interval_ms);

  // The rate limit strategy for the first matched request(i.e., its request header is matched with
  // bucket_matchers for the first time) should be already set based on no assignment behavior in
  // `createNewBucket` when the bucket is initially created.
  ASSERT(quota_buckets_.find(bucket_id) != quota_buckets_.end());
  if (quota_buckets_[bucket_id]
          ->bucket_action.quota_assignment_action()
          .rate_limit_strategy()
          .blanket_rule() == envoy::type::v3::RateLimitStrategy::ALLOW_ALL) {
    ENVOY_LOG(
        trace,
        "For first matched request with hashed bucket_id {}, it is allowed by ALLOW_ALL strategy.",
        bucket_id);
    return Http::FilterHeadersStatus::Continue;
  } else {
    // For the request that is rejected due to DENY_ALL no_assignment_behavior, immediate report is
    // still sent to RLQS server above, and here the local reply with deny response is sent.
    ENVOY_LOG(
        trace,
        "For first matched request with hashed bucket_id {}, it is throttled by DENY_ALL strategy.",
        bucket_id);
    sendDenyResponse();
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }
}

Http::FilterHeadersStatus
RateLimitQuotaFilter::processCachedBucket(size_t bucket_id,
                                          const RateLimitOnMatchAction& match_action) {
  // First, check if assignment has expired nor not.
  auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source_.monotonicTime().time_since_epoch());
  auto assignment_time_elapsed = Protobuf::util::TimeUtil::NanosecondsToDuration(
      (now - std::chrono::duration_cast<std::chrono::nanoseconds>(
                 quota_buckets_[bucket_id]->first_assignment_time.time_since_epoch()))
          .count());
  if (assignment_time_elapsed > quota_buckets_[bucket_id]
                                    ->bucket_action.quota_assignment_action()
                                    .assignment_time_to_live()) {
    // If expired, remove the cache entry.
    quota_buckets_.erase(bucket_id);

    // Default strategy is fail-Open (i.e., allow_all).
    auto ret_status = Envoy::Http::FilterHeadersStatus::Continue;
    // Check the expired assignment behavior if configured.
    // Note, only fail-open and fail-close are supported, more advanced expired assignment can be
    // supported as needed.
    if (match_action.bucketSettings().has_expired_assignment_behavior()) {
      if (match_action.bucketSettings()
              .expired_assignment_behavior()
              .fallback_rate_limit()
              .blanket_rule() == envoy::type::v3::RateLimitStrategy::DENY_ALL) {
        sendDenyResponse();
        ret_status = Envoy::Http::FilterHeadersStatus::StopIteration;
      }
    }

    return ret_status;
  }

  // Second, get the quota assignment (if exists) from the cached bucket action.
  if (quota_buckets_[bucket_id]->bucket_action.has_quota_assignment_action()) {
    auto rate_limit_strategy =
        quota_buckets_[bucket_id]->bucket_action.quota_assignment_action().rate_limit_strategy();

    // TODO(tyxia) Currently, blanket rule and token bucket strategies are implemented.
    // Change to switch case when `RequestsPerTimeUnit` strategy is implemented.
    if (rate_limit_strategy.has_blanket_rule()) {
      if (rate_limit_strategy.blanket_rule() == envoy::type::v3::RateLimitStrategy::ALLOW_ALL) {
        quota_buckets_[bucket_id]->quota_usage.num_requests_allowed += 1;
        ENVOY_LOG(trace,
                  "Request with hashed bucket_id {} is allowed by cached ALLOW_ALL strategy.",
                  bucket_id);
      } else if (rate_limit_strategy.blanket_rule() ==
                 envoy::type::v3::RateLimitStrategy::DENY_ALL) {
        quota_buckets_[bucket_id]->quota_usage.num_requests_denied += 1;
        ENVOY_LOG(trace,
                  "Request with hashed bucket_id {} is throttled by cached DENY_ALL strategy.",
                  bucket_id);
        sendDenyResponse();
        return Envoy::Http::FilterHeadersStatus::StopIteration;
      }
    } else if (rate_limit_strategy.has_token_bucket()) {
      ASSERT(quota_buckets_[bucket_id]->token_bucket_limiter != nullptr);
      TokenBucket* limiter = quota_buckets_[bucket_id]->token_bucket_limiter.get();
      // Try to consume 1 token from the bucket.
      if (limiter->consume(1, /*allow_partial=*/false)) {
        // Request is allowed.
        quota_buckets_[bucket_id]->quota_usage.num_requests_allowed += 1;
        ENVOY_LOG(trace, "Request with hashed bucket_id {} is allowed by token bucket limiter.",
                  bucket_id);
      } else {
        // Request is throttled.
        quota_buckets_[bucket_id]->quota_usage.num_requests_denied += 1;
        ENVOY_LOG(trace, "Request with hashed bucket_id {} is throttled by token bucket limiter",
                  bucket_id);
        sendDenyResponse();
        return Envoy::Http::FilterHeadersStatus::StopIteration;
      }
    }
  }
  return Envoy::Http::FilterHeadersStatus::Continue;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

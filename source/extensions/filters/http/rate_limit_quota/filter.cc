#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::type::v3::RateLimitStrategy;

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  // First, perform the request matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);
  if (!match_result.ok()) {
    // When the request is not matched by any matchers, it is ALLOWED by default (i.e., fail-open)
    // and its quota usage will not be reported to RLQS server.
    // TODO(tyxia) Add stats here and other places throughout the filter (if needed).
    ENVOY_LOG(debug,
              "The request is not matched by any matchers: ", match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Second, generate the bucket id for this request based on match action when the request matching
  // succeeds.
  const RateLimitOnMatchAction& match_action =
      match_result.value()->getTyped<RateLimitOnMatchAction>();
  auto ret = match_action.generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request, the request is ALLOWED by
    // default (i.e., fail-open).
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id_proto = ret.value();
  const size_t bucket_id = MessageUtil::hash(bucket_id_proto);
  if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
    // For first matched request, create a new bucket in the cache and sent the report to RLQS
    // server immediately.
    createNewBucket(bucket_id_proto, bucket_id);
    return sendImmediateReport(bucket_id, match_action);
  } else {
    // Found the cached bucket entry.
    // First, get the quota assignment (if exists) from the cached bucket action.
    // TODO(tyxia) Implement other assignment type besides ALLOW ALL.
    if (quota_buckets_[bucket_id]->bucket_action.has_quota_assignment_action()) {
      auto rate_limit_strategy =
          quota_buckets_[bucket_id]->bucket_action.quota_assignment_action().rate_limit_strategy();

      if (rate_limit_strategy.has_blanket_rule() &&
          rate_limit_strategy.blanket_rule() == envoy::type::v3::RateLimitStrategy::ALLOW_ALL) {
        quota_buckets_[bucket_id]->quota_usage.num_requests_allowed += 1;
      }
    }
  }
  return Envoy::Http::FilterHeadersStatus::Continue;
}

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMatchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext> factory(
      context, factory_context_.getServerFactoryContext(), visitor_);
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
  if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

void RateLimitQuotaFilter::onDestroy() {
  // TODO(tyxia) TLS resource are not cleaned here.
}

void RateLimitQuotaFilter::createNewBucket(const BucketId& bucket_id, size_t id) {
  // The first matched request doesn't have quota assignment from the RLQS server yet, so the
  // action is performed based on pre-configured strategy from no assignment behavior config.
  // TODO(tyxia) Check no assignment logic for new bucket (i.e., first matched request). Default is
  // allow all.
  QuotaUsage quota_usage;
  quota_usage.num_requests_allowed = 1;
  quota_usage.num_requests_denied = 0;
  quota_usage.last_report = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source_.monotonicTime().time_since_epoch());

  // Create new bucket and store it into quota cache.
  std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();
  new_bucket->bucket_id = bucket_id;
  new_bucket->quota_usage = quota_usage;
  quota_buckets_[id] = std::move(new_bucket);
}

Http::FilterHeadersStatus
RateLimitQuotaFilter::sendImmediateReport(const size_t bucket_id,
                                          const RateLimitOnMatchAction& match_action) {
  const auto& bucket_settings = match_action.bucketSettings();

  // Create the gRPC client if it has not been created.
  if (client_.rate_limit_client == nullptr) {
    client_.rate_limit_client = createRateLimitClient(factory_context_, config_->rlqs_server(),
                                                      this, quota_buckets_, config_->domain());
  } else {
    // Callback has been reset to nullptr when filter was destroyed last time.
    // Reset it here when new filter has been created.
    client_.rate_limit_client->setCallback(this);
  }

  // It can not be nullptr based on current implementation.
  ASSERT(client_.rate_limit_client != nullptr);

  // Start the streaming on the first request.
  auto status = client_.rate_limit_client->startStream(callbacks_->streamInfo());
  if (!status.ok()) {
    ENVOY_LOG(error, "Failed to start the gRPC stream: ", status.message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  initiating_call_ = true;

  // Send the usage report to RLQS server immediately on the first time when the request is
  // matched.
  client_.rate_limit_client->sendUsageReport(bucket_id);

  ASSERT(client_.send_reports_timer != nullptr);
  // Set the reporting interval and enable the timer.
  const int64_t reporting_interval = PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
  client_.send_reports_timer->enableTimer(std::chrono::milliseconds(reporting_interval));

  initiating_call_ = false;
  // TODO(tyxia) Revisit later.
  // Stop the iteration for headers as well as data and trailers for the current filter and the
  // filters following.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::type::v3::RateLimitStrategy;

Http::FilterHeadersStatus
RateLimitQuotaFilter::createNewBucketAndSendReport(const BucketId& bucket_id,
                                                   const RateLimitOnMatchAction& match_action) {
  const auto& bucket_settings = match_action.bucketSettings();
  // The first matched request that doesn't have quota assignment from the RLQS server yet, so the
  // action is performed based on pre-configured strategy from no assignment behavior config.
  // TODO(tyxia) Implement no assignment logic with the allow/deny interface
  // if (bucket_settings.has_no_assignment_behavior()) {
  //   // Retrieve the `blanket_rule` value from the config to decide if we want to fail-open or
  //   // fail-close.
  //   auto strategy = bucket_settings.no_assignment_behavior().fallback_rate_limit();
  //   if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
  //   }
  // } else {
  //   ENVOY_LOG(error, "No assignment behavior is not configured.");
  //   // We just use fail-open (i.e. ALLOW_ALL) here.
  //   return Envoy::Http::FilterHeadersStatus::Continue;
  // }
  // Allocate new bucket.
  std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();

  // Create the gRPC client if it has not been created.
  if (client_.rate_limit_client == nullptr) {
    client_.rate_limit_client = createRateLimitClient(factory_context_, config_->rlqs_server(),
                                                      this, quota_buckets_, quota_usage_reports_);
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
  // matched.l
  client_.rate_limit_client->sendUsageReport(config_->domain(), bucket_id);

  // Store the bucket into the quota bucket container.
  quota_buckets_[bucket_id] = std::move(new_bucket);

  ASSERT(client_.send_reports_timer != nullptr);
  // Set the reporting interval and enable the timer.
  const int64_t reporting_interval = PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
  client_.send_reports_timer->enableTimer(std::chrono::milliseconds(reporting_interval));

  initiating_call_ = false;
  // TODO(tyxia) Do we need to stop here???
  // Stop the iteration for headers as well as data and trailers for the current filter and the
  // filters following.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

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

  BucketId bucket_id = ret.value();
  if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
    // The request has been matched to the quota bucket for the first time.
    return createNewBucketAndSendReport(bucket_id, match_action);
  }
  // TODO(tyxia) Uncomment this section to finish the implementation and test.
  // else {
  // // Found the cached bucket entry.
  // // First, get the quota assignment (if exists) from the cached bucket action.
  // if (quota_buckets_[bucket_id].bucket_action.has_quota_assignment_action()) {
  //   auto rate_limit_strategy =
  //       quota_buckets_[bucket_id].bucket_action.quota_assignment_action().rate_limit_strategy();
  //   // Set up the action based on strategy.
  //   switch (rate_limit_strategy.strategy_case()) {
  //   case RateLimitStrategy::StrategyCase::kBlanketRule: {
  //   }
  //   case RateLimitStrategy::StrategyCase::kRequestsPerTimeUnit: {
  //     if (quota_buckets_[bucket_id].quota_usage.num_requests_allowed() <
  //         rate_limit_strategy.requests_per_time_unit().requests_per_time_unit()) {
  //     }
  //   }

  //   case RateLimitStrategy::StrategyCase::kTokenBucket: {
  //   }
  //   case RateLimitStrategy::StrategyCase::STRATEGY_NOT_SET: {
  //     PANIC_DUE_TO_PROTO_UNSET;
  //   }
  //     PANIC_DUE_TO_CORRUPT_ENUM;
  //   }
  // } else {
  // }
  // }

  // Get the quota usage info.
  // quota_buckets_[bucket_id].quota_usage;
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
    // TODO(tyxia) Update the logic with the CEL matcher and other attributes besides header.
    if (!headers.empty()) {
      data_ptr_->onRequestHeaders(headers);
    }
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
  // TODO(tyxia) Clean up resource. rate limit clients should be closed at TLS.
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

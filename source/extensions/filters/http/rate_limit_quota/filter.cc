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
  const RateLimitOnMatchAction* match_action =
      dynamic_cast<RateLimitOnMatchAction*>(match_result.value().get());
  auto ret = match_action->generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request, the request is ALLOWED by
    // default (i.e., fail-open).
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id = ret.value();

  if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
    // The request has been matched to the quota bucket for the first time.
    auto bucket_settings = match_action->bucketSettings();
    // The first matched request that doesn't have quota assignment from the RLQS server yet, so the
    // action is performed based on pre-configured strategy from no assignment behavior config.
    if (bucket_settings.has_no_assignment_behavior()) {
      // Retrieve the `blanket_rule` value from the config to decide if we want to fail-open or
      // fail-close.
      auto strategy = bucket_settings.no_assignment_behavior().fallback_rate_limit();
      if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
        // TODO(tyxia) Implement the allow/deny interface
      }
    } else {
      // TODO(tyxia) Change it to error, debug for testing
      ENVOY_LOG(debug, "No assignment behavior is not configured.");
      // We just use fail-open (i.e. ALLOW_ALL) here.
      // return Envoy::Http::FilterHeadersStatus::Continue;
    }

    Bucket new_bucket = {};
    // Create the gRPC client.
    new_bucket.rate_limit_client_ = createRateLimitClient(
        factory_context_, config_->rlqs_server(), *this, quota_buckets_, quota_usage_reports_);
    // TODO(tyxia) nullptr check.
    // Start the streaming on the first request.
    auto status = new_bucket.rate_limit_client_->startStream(callbacks_->streamInfo());
    if (!status.ok()) {
      ENVOY_LOG(error, "Failed to start the gRPC stream: ", status.message());
      return Envoy::Http::FilterHeadersStatus::Continue;
    }

    initiating_call_ = true;
    // Send the usage report to RLQS server immediately on the first time when the request is
    // matched.
    // TODO(tyxia) Due to the lifetime concern of the bucket_id, I switch from pointer to
    // absl::optional
    new_bucket.rate_limit_client_->sendUsageReport(config_->domain(), bucket_id);

    // Set up the quota usage report method that sends the reports the RLS server periodically.
    new_bucket.send_reports_timer =
        // TODO(tyxia) Timer should be on localThread dispatcher !!!
        // decode callback's dispatcher ----- worker thread dispatcher!!!!
        callbacks_->dispatcher().createTimer([&new_bucket, this]() -> void {
          ASSERT(new_bucket.rate_limit_client_ != nullptr);
          // No `BucketID` is provided (i.e., absl::nullopt) in periodical usage reports.
          new_bucket.rate_limit_client_->sendUsageReport(config_->domain(), absl::nullopt);
        });
    // Set the reporting interval.
    const int64_t reporting_interval =
        PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
    new_bucket.send_reports_timer->enableTimer(std::chrono::milliseconds(reporting_interval));

    // Store the bucket into the buckets map.
    quota_buckets_[bucket_id] = std::move(new_bucket);
    initiating_call_ = false;
    // TODO(tyxia) Do we need to stop here???
    // Stop the iteration for headers as well as data and trailers for the current filter and the
    // filters following.
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  } else {
    // TODO(tyxia) consider move to a new function. reduce the function length multiple places.
    // Found the cached bucket entry.
    // First, get the cached bucket action from the RLQS server.
    if (quota_buckets_[bucket_id].bucket_action.has_quota_assignment_action()) {
      auto rate_limit_strategy =
          quota_buckets_[bucket_id].bucket_action.quota_assignment_action().rate_limit_strategy();
      switch (rate_limit_strategy.strategy_case()) {
      case RateLimitStrategy::StrategyCase::kBlanketRule: {
      }
      case RateLimitStrategy::StrategyCase::kRequestsPerTimeUnit: {
        if (quota_buckets_[bucket_id].quota_usage.num_requests_allowed() <
            rate_limit_strategy.requests_per_time_unit().requests_per_time_unit()) {

            }
      }

      case RateLimitStrategy::StrategyCase::kTokenBucket: {
        // // TODO(tyxia) Look into token bucket algortihm.
        // // Token bucket ratelimiter 1)google3/third_party/envoy/src/source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.cc
        // // 2)google3/dos/quotas/bouncer/client/internal/rate_limiter/token_bucket_rate_limiter.cc

        // // Maybe have token_bucket_rate_limiter class
        // auto token_bucket = rate_limit_strategy.token_bucket();
        // uint32_t max_tokens = token_bucket.max_tokens();
        // uint32_t tokens_per_fill = token_bucket.tokens_per_fill().value();
        // // TODO(tyxia) Need to implement requestAllowedHelper
        // // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.cc;rcl=508241966;l=168
      }
      case RateLimitStrategy::StrategyCase::STRATEGY_NOT_SET: {
        PANIC_DUE_TO_PROTO_UNSET;
      }
        PANIC_DUE_TO_CORRUPT_ENUM;
      }
    } else {

    }
  }
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
    data_ptr_->onRequestHeaders(headers);
    // TODO(tyxia) This function should trigger the CEL expression matching. Here, we need to
    // implement the custom_matcher and factory, also statically register it so that CEL matching
    // will be triggered with its own match() method.
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
      return absl::InternalError("Unable to match the request.");
    }
  }
}

void RateLimitQuotaFilter::onQuotaResponse(RateLimitQuotaResponse&) {
  if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
  // TODO(tyxia) Remove!! The updates are performed on site
  // We can perform other post-processing though
  // for (auto action : response.bucket_action()) {
  //   if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
  //     ENVOY_LOG(error, "Received a Response whose bucket action is missing its bucket_id: ",
  //               response.ShortDebugString());
  //     continue;
  //   }
  //   // TODO(tyxia) Lifetime issue, here response is the reference but i need to store it in to
  //   // cache. So we need to pass by reference??? or build it here.
  //   // There is no update here!!!
  //   (*quota_buckets_)[action.bucket_id()].bucket_action = BucketAction(action);
  // }
}

void RateLimitQuotaFilter::onComplete(const RateLimitQuotaBucketSettings&, RateLimitStatus) {}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

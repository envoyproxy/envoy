#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::type::v3::RateLimitStrategy;

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  // First, perform requset matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);

  if (!match_result.ok()) {
    // When the reequest is not matched by any matchers, request is ALLOWED by
    // default (i.e., fail-open) and will not be reported to RLQS server.
    ENVOY_LOG(debug,
              "The request is not matched by any matchers: ", match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Second, generate the bucket id for this request based on match action if the request matching
  // succeeded.
  // TODO(tyxia) dynamic_cast; target type must be a reference or pointer type to a defined class
  const RateLimitOnMactchAction* match_action =
      dynamic_cast<RateLimitOnMactchAction*>(match_result.value().get());
  auto ret = match_action->generateBucketId(*data_ptr_, factory_context_, visitor_);
  if (!ret.ok()) {
    // When it failed to generate the bucket id for this specific request. the request is ALLOWED by
    // default (i.e., fail-open) and will not be reported to RLQS server.
    ENVOY_LOG(error, "Unable to generate the bucket id: ", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id = ret.value();
  ASSERT(quota_bucket_ != nullptr);
  // Third, look up the quota bucket cache.
  if (quota_bucket_->find(bucket_id) == quota_bucket_->end()) {
    // The request has been matched to the bucket successfully for the first time.
    auto bucket_settings = match_action->bucketSettings();
    // TODO(tyxia) For the first matched request that doesn't have quota assignment from the RLQS
    // server, we get the pre-configured rate limiting strategy from no assignment behavior in
    // the configuration.
    if (bucket_settings.has_no_assignment_behavior()) {
      // Retrieve the `blanket_rule` value from the config to decide if we want to fail-open or
      // fail-close.
      auto strategy = bucket_settings.no_assignment_behavior().fallback_rate_limit();
      if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
        // TODO(tyxia) Implement the allow/deny interface
      }
    } else {
      ENVOY_LOG(error, "no assignment behavior for is not configured.");
      // We just use fail-open (i.e. ALLOW_ALL) here.
    }

    // Build the quota bucket element.
    BucketElement element;
    //  Create the gRPC client and start the stream on the first request.
    element.rate_limit_client_ = createRateLimitClient(factory_context_, config_->rlqs_server(), quota_usage_reports_);
    auto status = element.rate_limit_client_->startStream(callbacks_->streamInfo());
    if (!status.ok()) {
      ENVOY_LOG(error, "Failed to start the gRPC stream: ", status.message());
      return Envoy::Http::FilterHeadersStatus::Continue;
    }
    // Send the usage report to RLQS immediately on the first time when the request is matched.
    // TODO(tyxia) Due to the lifetime concern of the bucket_id, I switch from pointer to
    // absl::optional
    element.rate_limit_client_->sendUsageReport(config_->domain(), bucket_id);

    // Set up the quota usage report method that sends the reports the RLS server periodically.
    // TODO(tyxia) If each bucket has its own timer callback, do we still need to build a list of
    // reports?
    // Create and enable the report timer callback on the first request.
    // TODO(tyxia) what is the behavior on the first request while waiting for the response
    /////// Moved from RateLimitQuotaFilter constructor.
    // Create the timer object to periodically sent the usage report to the RLS server.
    // TODO(tyxia) Timer callback will need to be outside of filter so that when filter is destoryed
    // i.e., request ends, we still have it to send the reports periodically???
    // Maybe assoiciated with the thread local storage????
    element.send_reports_timer =
    // TODO(tyxia) Timer should be on localThread dispatcher !!!
    // decodecallback's dispatcher ----- worker thread dispatcher!!!!
        callbacks_->dispatcher().createTimer([&element, this]() -> void {
          ASSERT(element.rate_limit_client_ != nullptr);
          // TODO(tyxia) For periodical send behavior, we just pass in nullopt argument.
          element.rate_limit_client_->sendUsageReport(config_->domain(), absl::nullopt);
        });
        // factory_context_.mainThreadDispatcher().createTimer([&element, this]() -> void {
        //   ASSERT(element.rate_limit_client_ != nullptr);
        //   // TODO(tyxia) For periodical send behavior, we just pass in nullopt.
        //   element.rate_limit_client_->sendUsageReport(config_->domain(), absl::nullopt);
        // });
    // Set the reporting interval.
    const int64_t reporting_interval =
        PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
    element.send_reports_timer->enableTimer(std::chrono::milliseconds(reporting_interval));

    // Store it into the buckets
    (*quota_bucket_)[bucket_id] = std::move(element);

    // TODO(tyxia) For the first request, we don't need to wait for the quota assignment from the
    // RLQS server because we already have the no_assignment behavior. Continue the filter chain
    // iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  } else {
    // The existing entry for this request has been found in the quota cache.
    // BucketElement elem = (*quota_bucket_)[bucket_id];
    auto bucket_action = (*quota_bucket_)[bucket_id].bucket_action;
    if (bucket_action.has_quota_assignment_action()) {
      auto quota_action = bucket_action.quota_assignment_action();
      if (!quota_action.has_rate_limit_strategy()) {
        // Nothing to do probabaly.
      }

      // Retrieve the rate limiting stragtegy.
      RateLimitStrategy strategy = quota_action.rate_limit_strategy();
      if (strategy.has_blanket_rule() && strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
        // TODO(tyxia) Keep track of #num of requests allowed, #num of requests denied.
        return Envoy::Http::FilterHeadersStatus::Continue;
      }
    }
  }

  //rate_limit_client_->rateLimit(*this);

  return Envoy::Http::FilterHeadersStatus::Continue;
}

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMactchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMactchActionContext> factory(
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
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo().downstreamAddressProvider());
    } else {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher tree has not been initialized yet");
  } else {
    data_ptr_->onRequestHeaders(headers);
    // TODO(tyxia) This function should trigger the CEL expression matching. Here, we need to
    // implement the custom_matcher and factory, also statically register it so that CEL matching
    // will be triggered with its own match() method.
    auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // return the matched result for `on_match` case.
        return match_result.result_();
      } else {
        return absl::NotFoundError("The match was completed, no match found");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match the request");
    }
  }
}

struct RateLimitResponseValue {
  // This request went above the configured limits for the rate limit filter.
  const std::string RateLimited = "request_rate_limited";
  // The rate limiter encountered a failure, and was configured to fail-closed.
  const std::string RateLimitError = "rate_limiter_error";
};
using RateLimitResponseDetails = ConstSingleton<RateLimitResponseValue>;

static inline Http::Code toErrorCode(uint64_t status) {
  const auto code = static_cast<Http::Code>(status);
  if (code >= Http::Code::BadRequest) {
    return code;
  }
  return Http::Code::TooManyRequests;
}

// TODO(tyxia) This is how we represent the RATELIMITED and ALLOWED
void RateLimitQuotaFilter::onComplete(const RateLimitQuotaBucketSettings& bucket_settings,
                                      RateLimitStatus status) {
  switch (status) {
  case RateLimitStatus::OK:
    break;
  case RateLimitStatus::OverLimit:
    // If the request has been `RateLimited`, send the local reply.
    auto deny_response_settings = bucket_settings.deny_response_settings();
    callbacks_->sendLocalReply(toErrorCode(deny_response_settings.http_status().code()),
                               std::string(deny_response_settings.http_body().value()), nullptr,
                               absl::nullopt, RateLimitResponseDetails::get().RateLimited);

    // The callback is nullptr at this moment.
    // Send local reply extra information want to be added in the local reply will add this
    // callback And it will be triggered when the local reply is constructed.
    // Response_header_to_add for ERROR Status Response for Error case
    // -- Rate limits exceed   response from envoy to downstream client
    // -- Like rate-limit-exceeded
    // Request for OK case
    // -- Request go through to be send to upstream
    break;
  }
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

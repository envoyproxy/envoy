#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  // First, perform request matching.
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching(headers);
  if (!match_result.ok()) {
    // When the request is not matched by any matchers, request is ALLOWED by
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
    ENVOY_LOG(debug, "Unable to generate the bucket id: {}", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id = ret.value();
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
    return absl::InternalError("Matcher tree has not been initialized yet.");
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
        return absl::NotFoundError("The match was completed, no match found.");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match the request.");
    }
  }
}

void RateLimitQuotaFilter::onComplete(const RateLimitQuotaBucketSettings&, RateLimitStatus) {}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

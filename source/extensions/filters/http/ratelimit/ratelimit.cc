#include "extensions/filters/http/ratelimit/ratelimit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/http/codes.h"
#include "common/http/header_utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

void Filter::initiateCall(const Http::HeaderMap& headers) {
  bool is_internal_request =
      headers.EnvoyInternalRequest() && (headers.EnvoyInternalRequest()->value() == "true");

  if ((is_internal_request && config_->requestType() == FilterRequestType::External) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::Internal)) {
    return;
  }

  Router::RouteConstSharedPtr route = callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  cluster_ = callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }

  std::vector<RateLimit::Descriptor> descriptors;

  const Router::RouteEntry* route_entry = route->routeEntry();
  // Get all applicable rate limit policy entries for the route.
  populateRateLimitDescriptors(route_entry->rateLimitPolicy(), descriptors, route_entry, headers);

  // Get all applicable rate limit policy entries for the virtual host if the route opted to
  // include the virtual host rate limits.
  if (route_entry->includeVirtualHostRateLimits()) {
    populateRateLimitDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors,
                                 route_entry, headers);
  }

  if (!descriptors.empty()) {
    state_ = State::Calling;
    initiating_call_ = true;
    client_->limit(*this, config_->domain(), descriptors, callbacks_->activeSpan());
    initiating_call_ = false;
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  initiateCall(headers);
  return (state_ == State::Calling || state_ == State::Responded)
             ? Http::FilterHeadersStatus::StopIteration
             : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  ASSERT(state_ != State::Responded);
  if (state_ != State::Calling) {
    return Http::FilterDataStatus::Continue;
  }
  // If the request is too large, stop reading new data until the buffer drains.
  return Http::FilterDataStatus::StopIterationAndWatermark;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? Http::FilterTrailersStatus::StopIteration
                                  : Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encode100ContinueHeaders(Http::HeaderMap&) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  addHeaders(headers);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) {}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::complete(RateLimit::LimitStatus status, Http::HeaderMapPtr&& headers) {
  state_ = State::Complete;
  headers_to_add_ = std::move(headers);

  switch (status) {
  case RateLimit::LimitStatus::OK:
    cluster_->statsScope().counter("ratelimit.ok").inc();
    break;
  case RateLimit::LimitStatus::Error:
    cluster_->statsScope().counter("ratelimit.error").inc();
    break;
  case RateLimit::LimitStatus::OverLimit:
    cluster_->statsScope().counter("ratelimit.over_limit").inc();
    Http::CodeUtility::ResponseStatInfo info{config_->scope(),
                                             cluster_->statsScope(),
                                             EMPTY_STRING,
                                             enumToInt(Http::Code::TooManyRequests),
                                             true,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             false};
    Http::CodeUtility::chargeResponseStat(info);
    headers_to_add_->insertEnvoyRateLimited().value(
        Http::Headers::get().EnvoyRateLimitedValues.True);
    break;
  }

  if (status == RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enforcing", 100)) {
    state_ = State::Responded;
    callbacks_->sendLocalReply(Http::Code::TooManyRequests, "",
                               [this](Http::HeaderMap& headers) { addHeaders(headers); },
                               config_->rateLimitedGrpcStatus());
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);
  } else if (status == RateLimit::LimitStatus::Error) {
    if (config_->failureModeAllow()) {
      cluster_->statsScope().counter("ratelimit.failure_mode_allowed").inc();
      if (!initiating_call_) {
        callbacks_->continueDecoding();
      }
    } else {
      state_ = State::Responded;
      callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt);
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError);
    }
  } else if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

void Filter::populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                          std::vector<RateLimit::Descriptor>& descriptors,
                                          const Router::RouteEntry* route_entry,
                                          const Http::HeaderMap& headers) const {
  for (const Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();
    if (!disable_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.http_filter_enabled", disable_key), 100)) {
      continue;
    }
    rate_limit.populateDescriptors(*route_entry, descriptors, config_->localInfo().clusterName(),
                                   headers, *callbacks_->streamInfo().downstreamRemoteAddress());
  }
}

void Filter::addHeaders(Http::HeaderMap& headers) {
  if (headers_to_add_) {
    Http::HeaderUtility::addHeaders(headers, *headers_to_add_);
    headers_to_add_ = nullptr;
  }
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

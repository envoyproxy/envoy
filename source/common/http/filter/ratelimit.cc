#include "ratelimit.h"

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

namespace Http {
namespace RateLimit {

const std::unique_ptr<const Http::HeaderMap> Filter::TOO_MANY_REQUESTS_HEADER{
    new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Code::TooManyRequests))}}};

void Filter::initiateCall(const HeaderMap& headers) {
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

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
  if (!cluster) {
    return;
  }
  cluster_ = cluster->info();

  std::vector<::RateLimit::Descriptor> descriptors;

  // Get all applicable rate limit policy entries for the route.
  populateRateLimitDescriptors(route_entry->rateLimitPolicy(), descriptors, route_entry, headers);

  // Get all applicable rate limit policy entries for the virtual host.
  populateRateLimitDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors,
                               route_entry, headers);

  if (!descriptors.empty()) {
    state_ = State::Calling;
    initiating_call_ = true;
    client_->limit(
        *this, config_->domain(), descriptors,
        {headers.RequestId() ? headers.RequestId()->value().c_str() : EMPTY_STRING,
         headers.OtSpanContext() ? headers.OtSpanContext()->value().c_str() : EMPTY_STRING});
    initiating_call_ = false;
  }
}

FilterHeadersStatus Filter::decodeHeaders(HeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return FilterHeadersStatus::Continue;
  }

  initiateCall(headers);
  return (state_ == State::Calling || state_ == State::Responded)
             ? FilterHeadersStatus::StopIteration
             : FilterHeadersStatus::Continue;
}

FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? FilterDataStatus::StopIterationAndBuffer
                                  : FilterDataStatus::Continue;
}

FilterTrailersStatus Filter::decodeTrailers(HeaderMap&) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? FilterTrailersStatus::StopIteration
                                  : FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks.addResetStreamCallback([this]() -> void {
    if (state_ == State::Calling) {
      client_->cancel();
    }
  });
}

void Filter::complete(::RateLimit::LimitStatus status) {
  state_ = State::Complete;

  switch (status) {
  case ::RateLimit::LimitStatus::OK:
    cluster_->statsScope().counter("ratelimit.ok").inc();
    break;
  case ::RateLimit::LimitStatus::Error:
    cluster_->statsScope().counter("ratelimit.error").inc();
    break;
  case ::RateLimit::LimitStatus::OverLimit:
    cluster_->statsScope().counter("ratelimit.over_limit").inc();
    Http::CodeUtility::ResponseStatInfo info{
        config_->globalStore(), cluster_->statsScope(), EMPTY_STRING, *TOO_MANY_REQUESTS_HEADER,
        true, EMPTY_STRING, EMPTY_STRING, EMPTY_STRING, EMPTY_STRING, false};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  if (status == ::RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enforcing", 100)) {
    state_ = State::Responded;
    Http::HeaderMapPtr response_headers{new HeaderMapImpl(*TOO_MANY_REQUESTS_HEADER)};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    callbacks_->requestInfo().setResponseFlag(Http::AccessLog::ResponseFlag::RateLimited);
  } else if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

void Filter::populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                          std::vector<::RateLimit::Descriptor>& descriptors,
                                          const Router::RouteEntry* route_entry,
                                          const HeaderMap& headers) const {
  for (const Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();
    if (!disable_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.http_filter_enabled", disable_key), 100)) {
      continue;
    }
    rate_limit.populateDescriptors(*route_entry, descriptors, config_->localInfo().clusterName(),
                                   headers, callbacks_->downstreamAddress());
  }
}

} // RateLimit
} // Http

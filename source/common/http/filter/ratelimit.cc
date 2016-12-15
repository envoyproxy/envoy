#include "ratelimit.h"

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

namespace Http {
namespace RateLimit {

const Http::HeaderMapPtr Filter::TOO_MANY_REQUESTS_HEADER{new Http::HeaderMapImpl{
    {Http::Headers::get().Status, std::to_string(enumToInt(Code::TooManyRequests))}}};

FilterHeadersStatus Filter::decodeHeaders(HeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return FilterHeadersStatus::Continue;
  }

  const Router::RouteEntry* route = callbacks_->routeTable().routeForRequest(headers);
  if (route) {
    const std::string& route_key = route->rateLimitPolicy().routeKey();
    if (!route_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.http_filter_enabled", route_key), 100)) {
      return FilterHeadersStatus::Continue;
    }

    std::vector<::RateLimit::Descriptor> descriptors;
    for (const Router::RateLimitPolicyEntry& rate_limit :
         route->rateLimitPolicy().getApplicableRateLimit(config_->stage())) {
      rate_limit.populateDescriptors(*route, descriptors, config_->localServiceCluster(), headers,
                                     callbacks_->downstreamAddress());
    }

    if (!descriptors.empty()) {
      cluster_stat_prefix_ = fmt::format("cluster.{}.", route->clusterName());
      cluster_ratelimit_stat_prefix_ = fmt::format("{}ratelimit.", cluster_stat_prefix_);

      state_ = State::Calling;
      initiating_call_ = true;
      client_->limit(*this, config_->domain(), descriptors,
                     headers.RequestId() ? headers.RequestId()->value().c_str() : EMPTY_STRING);
      initiating_call_ = false;
    }
  }

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
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "ok").inc();
    break;
  case ::RateLimit::LimitStatus::Error:
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "error").inc();
    break;
  case ::RateLimit::LimitStatus::OverLimit:
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "over_limit").inc();
    Http::CodeUtility::ResponseStatInfo info{config_->stats(), cluster_stat_prefix_,
                                             *TOO_MANY_REQUESTS_HEADER, true, EMPTY_STRING,
                                             EMPTY_STRING, EMPTY_STRING, EMPTY_STRING, false};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  if (status == ::RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enforcing", 100)) {
    state_ = State::Responded;
    Http::HeaderMapPtr response_headers{new HeaderMapImpl(*TOO_MANY_REQUESTS_HEADER)};
    callbacks_->encodeHeaders(std::move(response_headers), true);
  } else if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

} // RateLimit
} // Http

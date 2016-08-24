#include "ratelimit.h"

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/headers.h"

namespace Http {

const Http::HeaderMapImpl RateLimitFilter::TOO_MANY_REQUESTS_HEADER{
    {Http::Headers::get().Status, std::to_string(enumToInt(Code::TooManyRequests))}};

RateLimitFilterConfig::RateLimitFilterConfig(const Json::Object& config,
                                             const std::string& local_service_cluster,
                                             Stats::Store& stats_store, Runtime::Loader& runtime)
    : domain_(config.getString("domain")), local_service_cluster_(local_service_cluster),
      stats_store_(stats_store), runtime_(runtime) {}

FilterHeadersStatus RateLimitFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return FilterHeadersStatus::Continue;
  }

  const Router::RouteEntry* route = callbacks_->routeTable().routeForRequest(headers);
  if (route && route->rateLimitPolicy().doGlobalLimiting()) {
    cluster_stat_prefix_ = fmt::format("cluster.{}.", route->clusterName());
    cluster_ratelimit_stat_prefix_ = fmt::format("{}ratelimit.", cluster_stat_prefix_);

    // We limit on 2 dimensions.
    // 1) All calls to the given cluster.
    // 2) Calls to the given cluster and from this cluster.
    // The service side configuration can choose to limit on 1 or both of the above.
    // NOTE: In the future we might add more things such as the path of the request.
    std::vector<RateLimit::Descriptor> descriptors = {
        {{{"to_cluster", route->clusterName()}}},
        {{{"to_cluster", route->clusterName()}, {"from_cluster", config_->localServiceCluster()}}}};

    state_ = State::Calling;
    initiating_call_ = true;
    client_->limit(*this, config_->domain(), descriptors);
    initiating_call_ = false;
  }

  return (state_ == State::Calling || state_ == State::Responded)
             ? FilterHeadersStatus::StopIteration
             : FilterHeadersStatus::Continue;
}

FilterDataStatus RateLimitFilter::decodeData(Buffer::Instance&, bool) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? FilterDataStatus::StopIterationAndBuffer
                                  : FilterDataStatus::Continue;
}

FilterTrailersStatus RateLimitFilter::decodeTrailers(HeaderMap&) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? FilterTrailersStatus::StopIteration
                                  : FilterTrailersStatus::Continue;
}

void RateLimitFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks.addResetStreamCallback([this]() -> void {
    if (state_ == State::Calling) {
      client_->cancel();
    }
  });
}

void RateLimitFilter::complete(RateLimit::LimitStatus status) {
  state_ = State::Complete;

  switch (status) {
  case RateLimit::LimitStatus::OK:
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "ok").inc();
    break;
  case RateLimit::LimitStatus::Error:
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "error").inc();
    break;
  case RateLimit::LimitStatus::OverLimit:
    config_->stats().counter(cluster_ratelimit_stat_prefix_ + "over_limit").inc();
    Http::CodeUtility::ResponseStatInfo info{config_->stats(), cluster_stat_prefix_,
                                             TOO_MANY_REQUESTS_HEADER, true, EMPTY_STRING,
                                             EMPTY_STRING, EMPTY_STRING, EMPTY_STRING};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  if (status == RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enforcing", 100)) {
    state_ = State::Responded;
    Http::HeaderMapPtr response_headers{new HeaderMapImpl(TOO_MANY_REQUESTS_HEADER)};
    callbacks_->encodeHeaders(std::move(response_headers), true);
  } else if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

} // Http

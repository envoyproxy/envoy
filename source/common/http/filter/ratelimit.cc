#include "ratelimit.h"

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/headers.h"

namespace Http {
namespace RateLimit {

const Http::HeaderMapPtr Filter::TOO_MANY_REQUESTS_HEADER{new Http::HeaderMapImpl{
    {Http::Headers::get().Status, std::to_string(enumToInt(Code::TooManyRequests))}}};

void ServiceToServiceAction::populateDescriptors(const Router::RouteEntry& route,
                                                 std::vector<::RateLimit::Descriptor>& descriptors,
                                                 FilterConfig& config, const HeaderMap&,
                                                 StreamDecoderFilterCallbacks&) {
  // We limit on 2 dimensions.
  // 1) All calls to the given cluster.
  // 2) Calls to the given cluster and from this cluster.
  // The service side configuration can choose to limit on 1 or both of the above.
  descriptors.push_back({{{"to_cluster", route.clusterName()}}});
  descriptors.push_back(
      {{{"to_cluster", route.clusterName()}, {"from_cluster", config.localServiceCluster()}}});
}

void RequestHeadersAction::populateDescriptors(const Router::RouteEntry& route,
                                               std::vector<::RateLimit::Descriptor>& descriptors,
                                               FilterConfig&, const HeaderMap& headers,
                                               StreamDecoderFilterCallbacks&) {
  const HeaderEntry* header_value = headers.get(header_name_);
  if (!header_value) {
    return;
  }

  descriptors.push_back({{{descriptor_key_, header_value->value().c_str()}}});

  const std::string& route_key = route.rateLimitPolicy().routeKey();
  if (route_key.empty()) {
    return;
  }

  descriptors.push_back(
      {{{"route_key", route_key}, {descriptor_key_, header_value->value().c_str()}}});
}

void RemoteAddressAction::populateDescriptors(const Router::RouteEntry& route,
                                              std::vector<::RateLimit::Descriptor>& descriptors,
                                              FilterConfig&, const HeaderMap&,
                                              StreamDecoderFilterCallbacks& callbacks) {
  const std::string& remote_address = callbacks.downstreamAddress();
  if (remote_address.empty()) {
    return;
  }

  descriptors.push_back({{{"remote_address", remote_address}}});

  const std::string& route_key = route.rateLimitPolicy().routeKey();
  if (route_key.empty()) {
    return;
  }

  descriptors.push_back({{{"route_key", route_key}, {"remote_address", remote_address}}});
}

FilterConfig::FilterConfig(const Json::Object& config, const std::string& local_service_cluster,
                           Stats::Store& stats_store, Runtime::Loader& runtime)
    : domain_(config.getString("domain")), local_service_cluster_(local_service_cluster),
      stats_store_(stats_store), runtime_(runtime) {
  for (const Json::ObjectPtr& action : config.getObjectArray("actions")) {
    std::string type = action->getString("type");
    if (type == "service_to_service") {
      actions_.emplace_back(new ServiceToServiceAction());
    } else if (type == "request_headers") {
      actions_.emplace_back(new RequestHeadersAction(*action));
    } else if (type == "remote_address") {
      actions_.emplace_back(new RemoteAddressAction());
    } else {
      throw EnvoyException(fmt::format("unknown http rate limit filter action '{}'", type));
    }
  }
}

FilterHeadersStatus Filter::decodeHeaders(HeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return FilterHeadersStatus::Continue;
  }

  const Router::RouteEntry* route = callbacks_->routeTable().routeForRequest(headers);
  if (route && route->rateLimitPolicy().doGlobalLimiting()) {
    // Check if the route_key is enabled for rate limiting.
    const std::string& route_key = route->rateLimitPolicy().routeKey();
    if (!route_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.http_filter_enabled", route_key), 100)) {
      return FilterHeadersStatus::Continue;
    }

    std::vector<::RateLimit::Descriptor> descriptors;
    for (const ActionPtr& action : config_->actions()) {
      action->populateDescriptors(*route, descriptors, *config_, headers, *callbacks_);
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

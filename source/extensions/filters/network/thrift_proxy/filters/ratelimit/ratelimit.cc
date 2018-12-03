#include "extensions/filters/network/thrift_proxy/filters/ratelimit/ratelimit.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"
#include "extensions/filters/network/thrift_proxy/router/router_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

ThriftProxy::FilterStatus Filter::messageBegin(ThriftProxy::MessageMetadataSharedPtr metadata) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.thrift_filter_enabled", 100)) {
    return ThriftProxy::FilterStatus::Continue;
  }

  initiateCall(*metadata);
  return (state_ == State::Calling || state_ == State::Responded)
             ? ThriftProxy::FilterStatus::StopIteration
             : ThriftProxy::FilterStatus::Continue;
}

void Filter::initiateCall(const ThriftProxy::MessageMetadata& metadata) {
  ThriftProxy::Router::RouteConstSharedPtr route = callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const ThriftProxy::Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
  if (!cluster) {
    return;
  }
  cluster_ = cluster->info();

  std::vector<RateLimit::Descriptor> descriptors;

  // Get all applicable rate limit policy entries for the route.
  populateRateLimitDescriptors(route_entry->rateLimitPolicy(), descriptors, route_entry, metadata);

  if (!descriptors.empty()) {
    state_ = State::Calling;
    initiating_call_ = true;
    client_->limit(*this, config_->domain(), descriptors, Tracing::NullSpan::instance());
    initiating_call_ = false;
  }
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::complete(Filters::Common::RateLimit::LimitStatus status,
                      Http::HeaderMapPtr&& headers) {
  // TODO(zuercher): Store headers to append to a response. Adding them to a local reply (over
  // limit or error) is a matter of modifying the callbacks to allow it. Adding them to an upstream
  // response requires either response (aka encoder) filters or some other mechanism.
  UNREFERENCED_PARAMETER(headers);

  state_ = State::Complete;

  switch (status) {
  case Filters::Common::RateLimit::LimitStatus::OK:
    cluster_->statsScope().counter("ratelimit.ok").inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::Error:
    cluster_->statsScope().counter("ratelimit.error").inc();
    if (!config_->failureModeAllow()) {
      state_ = State::Responded;
      callbacks_->sendLocalReply(
          ThriftProxy::AppException(ThriftProxy::AppExceptionType::InternalError, "limiter error"),
          false);
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError);
      return;
    }
    cluster_->statsScope().counter("ratelimit.failure_mode_allowed").inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::OverLimit:
    cluster_->statsScope().counter("ratelimit.over_limit").inc();
    if (config_->runtime().snapshot().featureEnabled("ratelimit.thrift_filter_enforcing", 100)) {
      state_ = State::Responded;
      callbacks_->sendLocalReply(
          ThriftProxy::AppException(ThriftProxy::AppExceptionType::InternalError, "over limit"),
          false);
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);
      return;
    }
    break;
  }

  if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

void Filter::populateRateLimitDescriptors(
    const ThriftProxy::Router::RateLimitPolicy& rate_limit_policy,
    std::vector<RateLimit::Descriptor>& descriptors,
    const ThriftProxy::Router::RouteEntry* route_entry,
    const ThriftProxy::MessageMetadata& metadata) const {
  for (const ThriftProxy::Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();
    if (!disable_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.thrift_filter_enabled", disable_key), 100)) {
      continue;
    }
    rate_limit.populateDescriptors(*route_entry, descriptors, config_->localInfo().clusterName(),
                                   metadata, *callbacks_->streamInfo().downstreamRemoteAddress());
  }
}

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy

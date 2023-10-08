#include "source/extensions/filters/network/thrift_proxy/filters/ratelimit/ratelimit.h"

#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_ratelimit.h"

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
  ThriftProxy::Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const ThriftProxy::Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster =
      config_->cm().getThreadLocalCluster(route_entry->clusterName());
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
    client_->limit(*this, config_->domain(), descriptors, Tracing::NullSpan::instance(),
                   decoder_callbacks_->streamInfo(), 0);
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
                      Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses,
                      Http::ResponseHeaderMapPtr&& response_headers_to_add,
                      Http::RequestHeaderMapPtr&& request_headers_to_add, const std::string&,
                      Filters::Common::RateLimit::DynamicMetadataPtr&& dynamic_metadata) {
  // TODO(zuercher): Store headers to append to a response. Adding them to a local reply (over
  // limit or error) is a matter of modifying the callbacks to allow it. Adding them to an upstream
  // response requires either response (aka encoder) filters or some other mechanism.
  UNREFERENCED_PARAMETER(descriptor_statuses);
  UNREFERENCED_PARAMETER(response_headers_to_add);
  UNREFERENCED_PARAMETER(request_headers_to_add);

  if (dynamic_metadata != nullptr && !dynamic_metadata->fields().empty()) {
    decoder_callbacks_->streamInfo().setDynamicMetadata("envoy.filters.thrift.rate_limit",
                                                        *dynamic_metadata);
  }

  state_ = State::Complete;
  Filters::Common::RateLimit::StatNames& stat_names = config_->statNames();

  switch (status) {
  case Filters::Common::RateLimit::LimitStatus::OK:
    cluster_->statsScope().counterFromStatName(stat_names.ok_).inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::Error:
    cluster_->statsScope().counterFromStatName(stat_names.error_).inc();
    if (!config_->failureModeAllow()) {
      state_ = State::Responded;
      decoder_callbacks_->sendLocalReply(
          ThriftProxy::AppException(ThriftProxy::AppExceptionType::InternalError, "limiter error"),
          false);
      decoder_callbacks_->streamInfo().setResponseFlag(
          StreamInfo::ResponseFlag::RateLimitServiceError);
      return;
    }
    cluster_->statsScope().counterFromStatName(stat_names.failure_mode_allowed_).inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::OverLimit:
    cluster_->statsScope().counterFromStatName(stat_names.over_limit_).inc();
    if (config_->runtime().snapshot().featureEnabled("ratelimit.thrift_filter_enforcing", 100)) {
      state_ = State::Responded;
      decoder_callbacks_->sendLocalReply(
          ThriftProxy::AppException(ThriftProxy::AppExceptionType::InternalError, "over limit"),
          false);
      decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);
      return;
    }
    break;
  }

  if (!initiating_call_) {
    decoder_callbacks_->continueDecoding();
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
    rate_limit.populateDescriptors(
        *route_entry, descriptors, config_->localInfo().clusterName(), metadata,
        *decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress());
  }
}

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy

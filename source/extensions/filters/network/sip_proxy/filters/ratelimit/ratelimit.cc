//#include "extensions/filters/network/sip_proxy/filters/ratelimit/ratelimit.h"
//
//#include "common/tracing/http_tracer_impl.h"
//
//#include "extensions/filters/network/sip_proxy/app_exception_impl.h"
//#include "extensions/filters/network/sip_proxy/router/router.h"
//#include "extensions/filters/network/sip_proxy/router/router_ratelimit.h"
//
//namespace Envoy {
//namespace Extensions {
//namespace SipFilters {
//namespace RateLimitFilter {
//
//using namespace Envoy::Extensions::NetworkFilters;
//
//SipProxy::FilterStatus Filter::messageBegin(SipProxy::MessageMetadataSharedPtr metadata) {
//  if (!config_->runtime().snapshot().featureEnabled("ratelimit.sip_filter_enabled", 100)) {
//    return SipProxy::FilterStatus::Continue;
//  }
//
//  initiateCall(*metadata);
//  return (state_ == State::Calling || state_ == State::Responded)
//             ? SipProxy::FilterStatus::StopIteration
//             : SipProxy::FilterStatus::Continue;
//}
//
//void Filter::initiateCall(const SipProxy::MessageMetadata& metadata) {
//  SipProxy::Router::RouteConstSharedPtr route = decoder_callbacks_->route();
//  if (!route || !route->routeEntry()) {
//    return;
//  }
//
//  const SipProxy::Router::RouteEntry* route_entry = route->routeEntry();
//  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
//  if (!cluster) {
//    return;
//  }
//  cluster_ = cluster->info();
//
//  std::vector<RateLimit::Descriptor> descriptors;
//
//  // Get all applicable rate limit policy entries for the route.
//  populateRateLimitDescriptors(route_entry->rateLimitPolicy(), descriptors, route_entry, metadata);
//
//  if (!descriptors.empty()) {
//    state_ = State::Calling;
//    initiating_call_ = true;
//    client_->limit(*this, config_->domain(), descriptors, Tracing::NullSpan::instance());
//    initiating_call_ = false;
//  }
//}
//
//void Filter::onDestroy() {
//  if (state_ == State::Calling) {
//    state_ = State::Complete;
//    client_->cancel();
//  }
//}
//
//void Filter::complete(Filters::Common::RateLimit::LimitStatus status,
//                      Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses,
//                      Http::ResponseHeaderMapPtr&& response_headers_to_add,
//                      Http::RequestHeaderMapPtr&& request_headers_to_add) {
//  // TODO(zuercher): Store headers to append to a response. Adding them to a local reply (over
//  // limit or error) is a matter of modifying the callbacks to allow it. Adding them to an upstream
//  // response requires either response (aka encoder) filters or some other mechanism.
//  UNREFERENCED_PARAMETER(descriptor_statuses);
//  UNREFERENCED_PARAMETER(response_headers_to_add);
//  UNREFERENCED_PARAMETER(request_headers_to_add);
//
//  state_ = State::Complete;
//  Filters::Common::RateLimit::StatNames& stat_names = config_->statNames();
//
//  switch (status) {
//  case Filters::Common::RateLimit::LimitStatus::OK:
//    cluster_->statsScope().counterFromStatName(stat_names.ok_).inc();
//    break;
//  case Filters::Common::RateLimit::LimitStatus::Error:
//    cluster_->statsScope().counterFromStatName(stat_names.error_).inc();
//    if (!config_->failureModeAllow()) {
//      state_ = State::Responded;
//      decoder_callbacks_->sendLocalReply(
//          SipProxy::AppException(SipProxy::AppExceptionType::InternalError, "limiter error"),
//          false);
//      decoder_callbacks_->streamInfo().setResponseFlag(
//          StreamInfo::ResponseFlag::RateLimitServiceError);
//      return;
//    }
//    cluster_->statsScope().counterFromStatName(stat_names.failure_mode_allowed_).inc();
//    break;
//  case Filters::Common::RateLimit::LimitStatus::OverLimit:
//    cluster_->statsScope().counterFromStatName(stat_names.over_limit_).inc();
//    if (config_->runtime().snapshot().featureEnabled("ratelimit.sip_filter_enforcing", 100)) {
//      state_ = State::Responded;
//      decoder_callbacks_->sendLocalReply(
//          SipProxy::AppException(SipProxy::AppExceptionType::InternalError, "over limit"),
//          false);
//      decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);
//      return;
//    }
//    break;
//  }
//
//  if (!initiating_call_) {
//    decoder_callbacks_->continueDecoding();
//  }
//}
//
//void Filter::populateRateLimitDescriptors(
//    const SipProxy::Router::RateLimitPolicy& rate_limit_policy,
//    std::vector<RateLimit::Descriptor>& descriptors,
//    const SipProxy::Router::RouteEntry* route_entry,
//    const SipProxy::MessageMetadata& metadata) const {
//  for (const SipProxy::Router::RateLimitPolicyEntry& rate_limit :
//       rate_limit_policy.getApplicableRateLimit(config_->stage())) {
//    const std::string& disable_key = rate_limit.disableKey();
//    if (!disable_key.empty() &&
//        !config_->runtime().snapshot().featureEnabled(
//            fmt::format("ratelimit.{}.sip_filter_enabled", disable_key), 100)) {
//      continue;
//    }
//    rate_limit.populateDescriptors(*route_entry, descriptors, config_->localInfo().clusterName(),
//                                   metadata,
//                                   *decoder_callbacks_->streamInfo().downstreamRemoteAddress());
//  }
//}
//
//} // namespace RateLimitFilter
//} // namespace SipFilters
//} // namespace Extensions
//} // namespace Envoy

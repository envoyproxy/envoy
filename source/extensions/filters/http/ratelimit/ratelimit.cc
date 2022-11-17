#include "source/extensions/filters/http/ratelimit/ratelimit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/filters/http/ratelimit/ratelimit_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

struct RcDetailsValues {
  // This request went above the configured limits for the rate limit filter.
  const std::string RateLimited = "request_rate_limited";
  // The rate limiter encountered a failure, and was configured to fail-closed.
  const std::string RateLimitError = "rate_limiter_error";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

void Filter::initiateCall(const Http::RequestHeaderMap& headers) {
  const bool is_internal_request = Http::HeaderUtility::isEnvoyInternalRequest(headers);
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

  std::vector<Envoy::RateLimit::Descriptor> descriptors;

  const Router::RouteEntry* route_entry = route->routeEntry();
  // Get all applicable rate limit policy entries for the route.
  populateRateLimitDescriptors(route_entry->rateLimitPolicy(), descriptors, headers);

  VhRateLimitOptions vh_rate_limit_option = getVirtualHostRateLimitOption(route);

  switch (vh_rate_limit_option) {
  case VhRateLimitOptions::Ignore:
    break;
  case VhRateLimitOptions::Include:
    populateRateLimitDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors,
                                 headers);
    break;
  case VhRateLimitOptions::Override:
    if (route_entry->rateLimitPolicy().empty()) {
      populateRateLimitDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors,
                                   headers);
    }
    break;
  }

  if (!descriptors.empty()) {
    state_ = State::Calling;
    initiating_call_ = true;
    client_->limit(*this, config_->domain(), descriptors, callbacks_->activeSpan(),
                   callbacks_->streamInfo());
    initiating_call_ = false;
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (!config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  request_headers_ = &headers;
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

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  ASSERT(state_ != State::Responded);
  return state_ == State::Calling ? Http::FilterTrailersStatus::StopIteration
                                  : Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encode1xxHeaders(Http::ResponseHeaderMap&) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  populateResponseHeaders(headers, /*from_local_reply=*/false);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) {}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::complete(Filters::Common::RateLimit::LimitStatus status,
                      Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses,
                      Http::ResponseHeaderMapPtr&& response_headers_to_add,
                      Http::RequestHeaderMapPtr&& request_headers_to_add,
                      const std::string& response_body,
                      Filters::Common::RateLimit::DynamicMetadataPtr&& dynamic_metadata) {
  state_ = State::Complete;
  response_headers_to_add_ = std::move(response_headers_to_add);
  Http::HeaderMapPtr req_headers_to_add = std::move(request_headers_to_add);
  Stats::StatName empty_stat_name;
  Filters::Common::RateLimit::StatNames& stat_names = config_->statNames();

  if (dynamic_metadata != nullptr && !dynamic_metadata->fields().empty()) {
    callbacks_->streamInfo().setDynamicMetadata("envoy.filters.http.ratelimit", *dynamic_metadata);
  }

  switch (status) {
  case Filters::Common::RateLimit::LimitStatus::OK:
    cluster_->statsScope().counterFromStatName(stat_names.ok_).inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::Error:
    ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::filter), debug,
                        "rate limit status, status={}", static_cast<int>(status));
    cluster_->statsScope().counterFromStatName(stat_names.error_).inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::OverLimit:
    cluster_->statsScope().counterFromStatName(stat_names.over_limit_).inc();
    Http::CodeStats::ResponseStatInfo info{config_->scope(),
                                           cluster_->statsScope(),
                                           empty_stat_name,
                                           enumToInt(config_->rateLimitedStatus()),
                                           true,
                                           empty_stat_name,
                                           empty_stat_name,
                                           empty_stat_name,
                                           empty_stat_name,
                                           empty_stat_name,
                                           false};
    httpContext().codeStats().chargeResponseStat(info, false);
    if (config_->enableXEnvoyRateLimitedHeader()) {
      if (response_headers_to_add_ == nullptr) {
        response_headers_to_add_ = Http::ResponseHeaderMapImpl::create();
      }
      response_headers_to_add_->setReferenceEnvoyRateLimited(
          Http::Headers::get().EnvoyRateLimitedValues.True);
    }
    break;
  }

  if (config_->enableXRateLimitHeaders()) {
    Http::ResponseHeaderMapPtr rate_limit_headers =
        XRateLimitHeaderUtils::create(std::move(descriptor_statuses));
    if (response_headers_to_add_ == nullptr) {
      response_headers_to_add_ = Http::ResponseHeaderMapImpl::create();
    }
    Http::HeaderMapImpl::copyFrom(*response_headers_to_add_, *rate_limit_headers);
  } else {
    descriptor_statuses = nullptr;
  }

  if (status == Filters::Common::RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.http_filter_enforcing", 100)) {
    state_ = State::Responded;
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);
    callbacks_->sendLocalReply(
        config_->rateLimitedStatus(), response_body,
        [this](Http::HeaderMap& headers) {
          populateResponseHeaders(headers, /*from_local_reply=*/true);
          config_->responseHeadersParser().evaluateHeaders(
              headers, *request_headers_, dynamic_cast<const Http::ResponseHeaderMap&>(headers),
              callbacks_->streamInfo());
        },
        config_->rateLimitedGrpcStatus(), RcDetails::get().RateLimited);
  } else if (status == Filters::Common::RateLimit::LimitStatus::Error) {
    if (config_->failureModeAllow()) {
      cluster_->statsScope().counterFromStatName(stat_names.failure_mode_allowed_).inc();
      if (!initiating_call_) {
        appendRequestHeaders(req_headers_to_add);
        callbacks_->continueDecoding();
      }
    } else {
      state_ = State::Responded;
      callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError);
      callbacks_->sendLocalReply(Http::Code::InternalServerError, response_body, nullptr,
                                 absl::nullopt, RcDetails::get().RateLimitError);
    }
  } else if (!initiating_call_) {
    appendRequestHeaders(req_headers_to_add);
    callbacks_->continueDecoding();
  }
}

void Filter::populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                          std::vector<RateLimit::Descriptor>& descriptors,
                                          const Http::RequestHeaderMap& headers) const {
  for (const Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();
    if (!disable_key.empty() &&
        !config_->runtime().snapshot().featureEnabled(
            fmt::format("ratelimit.{}.http_filter_enabled", disable_key), 100)) {
      continue;
    }
    rate_limit.populateDescriptors(descriptors, config_->localInfo().clusterName(), headers,
                                   callbacks_->streamInfo());
  }
}

void Filter::populateResponseHeaders(Http::HeaderMap& response_headers, bool from_local_reply) {
  if (response_headers_to_add_) {
    // If the ratelimit service is sending back the content-type header and we're
    // populating response headers for a local reply, overwrite the existing
    // content-type header.
    //
    // We do this because sendLocalReply initially sets content-type to text/plain
    // whenever the response body is non-empty, but we want the content-type coming
    // from the ratelimit service to be authoritative in this case.
    if (from_local_reply && !response_headers_to_add_->getContentTypeValue().empty()) {
      response_headers.remove(Http::Headers::get().ContentType);
    }
    Http::HeaderMapImpl::copyFrom(response_headers, *response_headers_to_add_);
    response_headers_to_add_ = nullptr;
  }
}

void Filter::appendRequestHeaders(Http::HeaderMapPtr& request_headers_to_add) {
  if (request_headers_to_add && request_headers_) {
    Http::HeaderMapImpl::copyFrom(*request_headers_, *request_headers_to_add);
    request_headers_to_add = nullptr;
  }
}

VhRateLimitOptions Filter::getVirtualHostRateLimitOption(const Router::RouteConstSharedPtr& route) {
  if (route->routeEntry()->includeVirtualHostRateLimits()) {
    vh_rate_limits_ = VhRateLimitOptions::Include;
  } else {
    const auto* specific_per_route_config =
        Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(callbacks_);
    if (specific_per_route_config != nullptr) {
      switch (specific_per_route_config->virtualHostRateLimits()) {
      case envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute::INCLUDE:
        vh_rate_limits_ = VhRateLimitOptions::Include;
        break;
      case envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute::IGNORE:
        vh_rate_limits_ = VhRateLimitOptions::Ignore;
        break;
      case envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute::OVERRIDE:
      default:
        vh_rate_limits_ = VhRateLimitOptions::Override;
      }
    } else {
      vh_rate_limits_ = VhRateLimitOptions::Override;
    }
  }
  return vh_rate_limits_;
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const auto* route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(decoder_callbacks_);

  // We can never assume that the configuration/route will not change between
  // decodeHeaders() and encodeHeaders().
  // Store the configuration because we will use it later in encodeHeaders().
  if (route_config != nullptr) {
    ASSERT(used_config_ == config_.get());
    used_config_ = route_config; // Overwrite the used configuration.
  }

  if (!used_config_->enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  used_config_->stats().enabled_.inc();

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  if (used_config_->hasDescriptors()) {
    if (used_config_->hasRateLimitConfigs()) {
      used_config_->populateDescriptors(headers, decoder_callbacks_->streamInfo(), descriptors);
    } else {
      populateDescriptors(descriptors, headers);
    }
  }

  if (ENVOY_LOG_CHECK_LEVEL(debug)) {
    for (const auto& request_descriptor : descriptors) {
      ENVOY_LOG(debug, "populate descriptor: {}", request_descriptor.toString());
    }
  }

  auto result = requestAllowed(descriptors);
  // The global limiter, route limiter, or connection level limiter are all have longer life
  // than the request, so we can safely store the token bucket context reference.
  token_bucket_context_ = result.token_bucket_context;
  x_ratelimit_option_ = result.x_ratelimit_option;

  if (result.allowed) {
    used_config_->stats().ok_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  used_config_->stats().rate_limited_.inc();

  if (token_bucket_context_ != nullptr && token_bucket_context_->shadowMode()) {
    used_config_->stats().shadow_mode_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (!used_config_->enforced()) {
    used_config_->requestHeadersParser().evaluateHeaders(headers, decoder_callbacks_->streamInfo());
    return Http::FilterHeadersStatus::Continue;
  }

  used_config_->stats().enforced_.inc();

  decoder_callbacks_->sendLocalReply(
      used_config_->status(), "local_rate_limited",
      [this](Http::HeaderMap& headers) {
        used_config_->responseHeadersParser().evaluateHeaders(headers,
                                                              decoder_callbacks_->streamInfo());
      },
      used_config_->rateLimitedGrpcStatus(), "local_rate_limited");
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::RateLimited);

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  // We can never assume the decodeHeaders() was called before encodeHeaders().
  if (!token_bucket_context_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (enableXRateLimitHeaders()) {
    headers.addReferenceKey(
        Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit,
        token_bucket_context_->maxTokens());
    headers.addReferenceKey(
        Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining,
        token_bucket_context_->remainingTokens());
    headers.addReferenceKey(
        Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset,
        token_bucket_context_->resetSeconds());
  }

  return Http::FilterHeadersStatus::Continue;
}

Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl::Result
Filter::requestAllowed(absl::Span<const Envoy::RateLimit::Descriptor> request_descriptors) {
  return used_config_->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().requestAllowed(request_descriptors)
             : used_config_->requestAllowed(request_descriptors);
}

Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl&
Filter::getPerConnectionRateLimiter() {

  ASSERT(used_config_->rateLimitPerConnection());

  auto typed_state =
      decoder_callbacks_->streamInfo().filterState()->getDataReadOnly<PerConnectionRateLimiter>(
          PerConnectionRateLimiter::key());

  if (typed_state == nullptr) {
    auto limiter = std::make_shared<PerConnectionRateLimiter>(
        used_config_->fillInterval(), used_config_->maxTokens(), used_config_->tokensPerFill(),
        used_config_->maxDynamicDescriptors(), decoder_callbacks_->dispatcher(),
        used_config_->descriptors(), used_config_->consumeDefaultTokenBucket());

    decoder_callbacks_->streamInfo().filterState()->setData(
        PerConnectionRateLimiter::key(), limiter, StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
    return const_cast<Filters::Common::LocalRateLimit::LocalRateLimiterImpl&>(limiter->value());
  }

  return const_cast<Filters::Common::LocalRateLimit::LocalRateLimiterImpl&>(typed_state->value());
}

void Filter::populateDescriptors(std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                                 Http::RequestHeaderMap& headers) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  // Get all applicable rate limit policy entries for the route.
  populateDescriptors(route_entry->rateLimitPolicy(), descriptors, headers);
  VhRateLimitOptions vh_rate_limit_option = getVirtualHostRateLimitOption(route);

  switch (vh_rate_limit_option) {
  case VhRateLimitOptions::Ignore:
    return;
  case VhRateLimitOptions::Include:
    populateDescriptors(route->virtualHost()->rateLimitPolicy(), descriptors, headers);
    return;
  case VhRateLimitOptions::Override:
    if (route_entry->rateLimitPolicy().empty()) {
      populateDescriptors(route->virtualHost()->rateLimitPolicy(), descriptors, headers);
    }
    return;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void Filter::populateDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                 std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                                 Http::RequestHeaderMap& headers) {
  for (const Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(used_config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();

    if (!disable_key.empty()) {
      continue;
    }
    rate_limit.populateDescriptors(descriptors, used_config_->localInfo().clusterName(), headers,
                                   decoder_callbacks_->streamInfo());
  }
}

VhRateLimitOptions Filter::getVirtualHostRateLimitOption(const Router::RouteConstSharedPtr& route) {
  if (route->routeEntry()->includeVirtualHostRateLimits()) {
    vh_rate_limits_ = VhRateLimitOptions::Include;
  } else {
    switch (used_config_->virtualHostRateLimits()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::extensions::common::ratelimit::v3::INCLUDE:
      vh_rate_limits_ = VhRateLimitOptions::Include;
      break;
    case envoy::extensions::common::ratelimit::v3::IGNORE:
      vh_rate_limits_ = VhRateLimitOptions::Ignore;
      break;
    case envoy::extensions::common::ratelimit::v3::OVERRIDE:
      vh_rate_limits_ = VhRateLimitOptions::Override;
      break;
    }
  }
  return vh_rate_limits_;
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

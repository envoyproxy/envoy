#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/filters/http/common/ratelimit_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

const std::string& PerConnectionRateLimiter::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "per_connection_local_rate_limiter");
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, const bool per_route)
    : dispatcher_(dispatcher), status_(toErrorCode(config.status().code())),
      stats_(generateStats(config.stat_prefix(), scope)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config.token_bucket(), fill_interval, 0))),
      max_tokens_(config.token_bucket().max_tokens()),
      tokens_per_fill_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1)),
      descriptors_(config.descriptors()),
      rate_limit_per_connection_(config.local_rate_limit_per_downstream_connection()),
      always_consume_default_token_bucket_(
          config.has_always_consume_default_token_bucket()
              ? config.always_consume_default_token_bucket().value()
              : true),
      local_info_(local_info), runtime_(runtime),
      filter_enabled_(
          config.has_filter_enabled()
              ? absl::optional<Envoy::Runtime::FractionalPercent>(
                    Envoy::Runtime::FractionalPercent(config.filter_enabled(), runtime_))
              : absl::nullopt),
      filter_enforced_(
          config.has_filter_enabled()
              ? absl::optional<Envoy::Runtime::FractionalPercent>(
                    Envoy::Runtime::FractionalPercent(config.filter_enforced(), runtime_))
              : absl::nullopt),
      response_headers_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add()),
          Router::HeaderParserPtr)),
      request_headers_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.request_headers_to_add_when_not_enforced()),
          Router::HeaderParserPtr)),
      stage_(static_cast<uint64_t>(config.stage())),
      has_descriptors_(!config.descriptors().empty()),
      enable_x_rate_limit_headers_(config.enable_x_ratelimit_headers() ==
                                   envoy::extensions::common::ratelimit::v3::DRAFT_VERSION_03),
      vh_rate_limits_(config.vh_rate_limits()),
      rate_limited_grpc_status_(
          config.rate_limited_as_resource_exhausted()
              ? absl::make_optional(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted)
              : absl::nullopt) {
  // Note: no token bucket is fine for the global config, which would be the case for enabling
  //       the filter globally but disabled and then applying limits at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an no token
  //       bucket so we throw an error. If there's no token bucket configured globally or
  //       at the vhost/route level, no rate limiting is applied.
  if (per_route && !config.has_token_bucket()) {
    throw EnvoyException("local rate limit token bucket must be set for per filter configs");
  }

  Filters::Common::LocalRateLimit::ShareProviderSharedPtr share_provider;
  if (config.has_local_cluster_rate_limit()) {
    if (rate_limit_per_connection_) {
      throw EnvoyException("local_cluster_rate_limit is set and "
                           "local_rate_limit_per_downstream_connection is set to true");
    }
    if (!cm.localClusterName().has_value()) {
      throw EnvoyException("local_cluster_rate_limit is set but no local cluster name is present");
    }

    // If the local cluster name is set then the relevant cluster must exist or the cluster
    // manager will fail to initialize.
    share_provider_manager_ = Filters::Common::LocalRateLimit::ShareProviderManager::singleton(
        dispatcher, cm, singleton_manager);
    if (!share_provider_manager_) {
      throw EnvoyException("local_cluster_rate_limit is set but no local cluster is present");
    }

    share_provider = share_provider_manager_->getShareProvider(config.local_cluster_rate_limit());
  }

  rate_limiter_ = std::make_unique<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>(
      fill_interval_, max_tokens_, tokens_per_fill_, dispatcher, descriptors_,
      always_consume_default_token_bucket_, std::move(share_provider));
}

bool FilterConfig::requestAllowed(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  return rate_limiter_->requestAllowed(request_descriptors);
}

uint32_t
FilterConfig::maxTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  return rate_limiter_->maxTokens(request_descriptors);
}

uint32_t FilterConfig::remainingTokens(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  return rate_limiter_->remainingTokens(request_descriptors);
}

int64_t FilterConfig::remainingFillInterval(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  return rate_limiter_->remainingFillInterval(request_descriptors);
}

LocalRateLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_local_rate_limit";
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

bool FilterConfig::enabled() const {
  return filter_enabled_.has_value() ? filter_enabled_->enabled() : false;
}

bool FilterConfig::enforced() const {
  return filter_enforced_.has_value() ? filter_enforced_->enabled() : false;
}

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

  std::vector<RateLimit::LocalDescriptor> descriptors;
  if (used_config_->hasDescriptors()) {
    populateDescriptors(descriptors, headers);
  }

  // Store descriptors which is used to generate x-ratelimit-* headers in encoding response headers.
  stored_descriptors_ = descriptors;

  if (ENVOY_LOG_CHECK_LEVEL(debug)) {
    for (const auto& request_descriptor : descriptors) {
      for (const Envoy::RateLimit::DescriptorEntry& entry : request_descriptor.entries_) {
        ENVOY_LOG(debug, "populate descriptors: key={} value={}", entry.key_, entry.value_);
      }
    }
  }

  if (requestAllowed(descriptors)) {
    used_config_->stats().ok_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  used_config_->stats().rate_limited_.inc();

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
  if (used_config_->enabled() && used_config_->enableXRateLimitHeaders() &&
      stored_descriptors_.has_value()) {
    auto limit = maxTokens(stored_descriptors_.value());
    auto remaining = remainingTokens(stored_descriptors_.value());
    auto reset = remainingFillInterval(stored_descriptors_.value());

    headers.addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit, limit);
    headers.addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining, remaining);
    headers.addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset, reset);
  }

  return Http::FilterHeadersStatus::Continue;
}

bool Filter::requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) {
  return used_config_->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().requestAllowed(request_descriptors)
             : used_config_->requestAllowed(request_descriptors);
}

uint32_t Filter::maxTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) {
  return used_config_->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().maxTokens(request_descriptors)
             : used_config_->maxTokens(request_descriptors);
}

uint32_t Filter::remainingTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) {
  return used_config_->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().remainingTokens(request_descriptors)
             : used_config_->remainingTokens(request_descriptors);
}

int64_t
Filter::remainingFillInterval(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) {
  return used_config_->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().remainingFillInterval(request_descriptors)
             : used_config_->remainingFillInterval(request_descriptors);
}

const Filters::Common::LocalRateLimit::LocalRateLimiterImpl& Filter::getPerConnectionRateLimiter() {
  ASSERT(used_config_->rateLimitPerConnection());

  auto typed_state =
      decoder_callbacks_->streamInfo().filterState()->getDataReadOnly<PerConnectionRateLimiter>(
          PerConnectionRateLimiter::key());

  if (typed_state == nullptr) {
    auto limiter = std::make_shared<PerConnectionRateLimiter>(
        used_config_->fillInterval(), used_config_->maxTokens(), used_config_->tokensPerFill(),
        decoder_callbacks_->dispatcher(), used_config_->descriptors(),
        used_config_->consumeDefaultTokenBucket());

    decoder_callbacks_->streamInfo().filterState()->setData(
        PerConnectionRateLimiter::key(), limiter, StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
    return limiter->value();
  }

  return typed_state->value();
}

void Filter::populateDescriptors(std::vector<RateLimit::LocalDescriptor>& descriptors,
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
    populateDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors, headers);
    return;
  case VhRateLimitOptions::Override:
    if (route_entry->rateLimitPolicy().empty()) {
      populateDescriptors(route_entry->virtualHost().rateLimitPolicy(), descriptors, headers);
    }
    return;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void Filter::populateDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                 std::vector<RateLimit::LocalDescriptor>& descriptors,
                                 Http::RequestHeaderMap& headers) {
  for (const Router::RateLimitPolicyEntry& rate_limit :
       rate_limit_policy.getApplicableRateLimit(used_config_->stage())) {
    const std::string& disable_key = rate_limit.disableKey();

    if (!disable_key.empty()) {
      continue;
    }
    rate_limit.populateLocalDescriptors(descriptors, used_config_->localInfo().clusterName(),
                                        headers, decoder_callbacks_->streamInfo());
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

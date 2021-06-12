#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

const std::string& PerConnectionRateLimiter::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "per_connection_local_rate_limiter");
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher, Stats::Scope& scope,
    Runtime::Loader& runtime, const bool per_route)
    : status_(toErrorCode(config.status().code())),
      stats_(generateStats(config.stat_prefix(), scope)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config.token_bucket(), fill_interval, 0))),
      max_tokens_(config.token_bucket().max_tokens()),
      tokens_per_fill_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1)),
      descriptors_(config.descriptors()),
      rate_limit_per_connection_(config.local_rate_limit_per_downstream_connection()),
      rate_limiter_(Filters::Common::LocalRateLimit::LocalRateLimiterImpl(
          fill_interval_, max_tokens_, tokens_per_fill_, dispatcher, descriptors_)),
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
      response_headers_parser_(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add())),
      request_headers_parser_(Envoy::Router::HeaderParser::configure(
          config.request_headers_to_add_when_not_enforced())),
      stage_(static_cast<uint64_t>(config.stage())),
      has_descriptors_(!config.descriptors().empty()) {
  // Note: no token bucket is fine for the global config, which would be the case for enabling
  //       the filter globally but disabled and then applying limits at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an no token
  //       bucket so we throw an error. If there's no token bucket configured globally or
  //       at the vhost/route level, no rate limiting is applied.
  if (per_route && !config.has_token_bucket()) {
    throw EnvoyException("local rate limit token bucket must be set for per filter configs");
  }
}

bool FilterConfig::requestAllowed(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  return rate_limiter_.requestAllowed(request_descriptors);
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
  const auto* config = getConfig();

  if (!config->enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().enabled_.inc();

  std::vector<RateLimit::LocalDescriptor> descriptors;
  if (config->hasDescriptors()) {
    populateDescriptors(descriptors, headers);
  }

  if (requestAllowed(descriptors)) {
    config->stats().ok_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().rate_limited_.inc();

  if (!config->enforced()) {
    config->requestHeadersParser().evaluateHeaders(headers, decoder_callbacks_->streamInfo());
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().enforced_.inc();

  decoder_callbacks_->sendLocalReply(
      config->status(), "local_rate_limited",
      [this, config](Http::HeaderMap& headers) {
        config->responseHeadersParser().evaluateHeaders(headers, decoder_callbacks_->streamInfo());
      },
      absl::nullopt, "local_rate_limited");
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);

  return Http::FilterHeadersStatus::StopIteration;
}

bool Filter::requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) {
  const auto* config = getConfig();
  return config->rateLimitPerConnection()
             ? getPerConnectionRateLimiter().requestAllowed(request_descriptors)
             : config->requestAllowed(request_descriptors);
}

const Filters::Common::LocalRateLimit::LocalRateLimiterImpl& Filter::getPerConnectionRateLimiter() {
  const auto* config = getConfig();
  ASSERT(config->rateLimitPerConnection());

  if (!decoder_callbacks_->streamInfo().filterState()->hasData<PerConnectionRateLimiter>(
          PerConnectionRateLimiter::key())) {
    decoder_callbacks_->streamInfo().filterState()->setData(
        PerConnectionRateLimiter::key(),
        std::make_unique<PerConnectionRateLimiter>(
            config->fillInterval(), config->maxTokens(), config->tokensPerFill(),
            decoder_callbacks_->dispatcher(), config->descriptors()),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);
  }

  return decoder_callbacks_->streamInfo()
      .filterState()
      ->getDataReadOnly<PerConnectionRateLimiter>(PerConnectionRateLimiter::key())
      .value();
}

void Filter::populateDescriptors(std::vector<RateLimit::LocalDescriptor>& descriptors,
                                 Http::RequestHeaderMap& headers) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  // Get all applicable rate limit policy entries for the route.
  const auto* config = getConfig();
  for (const Router::RateLimitPolicyEntry& rate_limit :
       route_entry->rateLimitPolicy().getApplicableRateLimit(config->stage())) {
    const std::string& disable_key = rate_limit.disableKey();

    if (!disable_key.empty()) {
      continue;
    }
    rate_limit.populateLocalDescriptors(descriptors, config->localInfo().clusterName(), headers,
                                        decoder_callbacks_->streamInfo());
  }
}

const FilterConfig* Filter::getConfig() const {
  const auto* config = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      "envoy.filters.http.local_ratelimit", decoder_callbacks_->route());
  if (config) {
    return config;
  }

  return config_.get();
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

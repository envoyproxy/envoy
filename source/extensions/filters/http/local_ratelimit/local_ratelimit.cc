#include "extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
    const bool per_route)
    : status_(toErrorCode(config.status().code())),
      stats_(generateStats(config.stat_prefix(), scope)),
      rate_limiter_(Filters::Common::LocalRateLimit::LocalRateLimiterImpl(
          std::chrono::milliseconds(
              PROTOBUF_GET_MS_OR_DEFAULT(config.token_bucket(), fill_interval, 0)),
          config.token_bucket().max_tokens(),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1), dispatcher)),
      runtime_(runtime),
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
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add())) {
  // Note: no token bucket is fine for the global config, which would be the case for enabling
  //       the filter globally but disabled and then applying limits at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an no token
  //       bucket so we throw an error. If there's no token bucket configured globally or
  //       at the vhost/route level, no rate limiting is applied.
  if (per_route && !config.has_token_bucket()) {
    throw EnvoyException("local rate limit token bucket must be set for per filter configs");
  }
}

bool FilterConfig::requestAllowed() const { return rate_limiter_.requestAllowed(); }

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

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto* config = getConfig();

  if (!config->enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().enabled_.inc();

  if (config->requestAllowed()) {
    config->stats().ok_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  config->stats().rate_limited_.inc();

  if (!config->enforced()) {
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

#include "extensions/filters/http/local_ratelimit/local_ratelimit.h"
#include "extensions/filters/http/well_known_names.h"

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
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime)
    : status_(toErrorCode(config.status().code())),
      stats_(generateStats(config.stat_prefix(), scope)),
      max_tokens_(config.token_bucket().max_tokens()),
      tokens_per_fill_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1)),
      fill_interval_(config.has_token_bucket()
                         ? PROTOBUF_GET_MS_REQUIRED(config.token_bucket(), fill_interval)
                         : 0),
      fill_timer_(fill_interval_ > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr),
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
  if (fill_timer_ && fill_interval_ < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }

  tokens_ = max_tokens_;

  if (fill_timer_) {
    fill_timer_->enableTimer(fill_interval_);
  }
}

FilterConfig::~FilterConfig() {
  if (fill_timer_ != nullptr) {
    fill_timer_->disableTimer();
  }
}

LocalRateLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_local_rate_limit";
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

// Snitched from the local rate limit network filter.
// TODO(rgs1): reuse the code from above.
void FilterConfig::onFillTimer() {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  uint32_t new_tokens_value;
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    new_tokens_value = std::min(max_tokens_, expected_tokens + tokens_per_fill_);
    // Loop while the weak CAS fails trying to update the tokens value.
  } while (
      !tokens_.compare_exchange_weak(expected_tokens, new_tokens_value, std::memory_order_relaxed));

  fill_timer_->enableTimer(fill_interval_);
}

// Snitched from the local rate limit network filter.
// TODO(rgs1): reuse the code from above.
bool FilterConfig::requestAllowed() const {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    if (expected_tokens == 0) {
      return false;
    }
    // Loop while the weak CAS fails trying to subtract 1 from expected.
  } while (!tokens_.compare_exchange_weak(expected_tokens, expected_tokens - 1,
                                          std::memory_order_relaxed));

  // We successfully decremented the counter by 1.
  return true;
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
      config->status(), "",
      [this, config](Http::HeaderMap& headers) {
        config->responseHeadersParser().evaluateHeaders(headers, decoder_callbacks_->streamInfo());
      },
      absl::nullopt, "request_rate_limited");
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::RateLimited);

  return Http::FilterHeadersStatus::StopIteration;
}

const FilterConfig* Filter::getConfig() const {
  // Cached config pointer.
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      HttpFilterNames::get().LocalRateLimit, decoder_callbacks_->route());
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = config_.get();
  return effective_config_;
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

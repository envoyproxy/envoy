#include "contrib/generic_proxy/filters/network/source/local_ratelimit/local_ratelimit.h"

#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3/local_ratelimit.pb.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace LocalRateLimit {

LocalRateLimiterImpl::LocalRateLimiterImpl(
    Protobuf::RepeatedPtrField<RateLimitEntryProto>& rateLimitEntries,
    Envoy::Event::Dispatcher& dispatcher) {
  // The more specified rate_limits
  for (const auto& rate_limit_entry : rateLimitEntries) {
    std::unique_ptr<RateLimitEntry> new_rate_limit_entry = std::make_unique<RateLimitEntry>();
    auto matcher = std::make_unique<RequestMatchInputMatcher>(rate_limit_entry.matcher());
    new_rate_limit_entry->match_ = std::move(matcher);
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>
        descriptors;
    UnitTypeProto unit = rate_limit_entry.limiter().unit();
    std::chrono::milliseconds fill_interval = std::chrono::milliseconds(0);
    int unit_time, fill_time, fill_tokens, interval, tokens, min_time = 200;
    uint32_t max_tokens = rate_limit_entry.limiter().rate();
    uint32_t tokens_per_fill = 1;
    switch (unit) {
    case UnitTypeProto::SS: {
      unit_time = 1000; // seconds
      break;
    }
    case UnitTypeProto::MM: {
      unit_time = 1000 * 60; // minute
      break;
    }
    case UnitTypeProto::HH: {
      unit_time = 1000 * 60 * 60; // hour
      break;
    }
    case UnitTypeProto::DD: {
      unit_time = 1000 * 60 * 60 * 24; // day
      break;
    }
    default:
      throw EnvoyException("local rate limit unit must be in [SS, MM, HH, DD]");
    }

    // Through some logic to ensure the filling interval and the number of filling
    // tokens, the minimum filling interval is 200ms, and the minimum number of
    // filling tokens is one.
    interval = unit_time / min_time;
    tokens = max_tokens / interval;
    if (tokens < 1) {
      fill_time = unit_time / max_tokens;
      fill_tokens = 1;
    } else {
      fill_time = min_time;
      fill_tokens = tokens;
    }
    fill_interval = std::chrono::milliseconds(fill_time);
    tokens_per_fill = fill_tokens;

    auto limiter = std::make_unique<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>(
        fill_interval, max_tokens, tokens_per_fill, dispatcher, descriptors);
    new_rate_limit_entry->limiter_ = std::move(limiter);
    rate_limit_entries_.emplace_back(std::move(new_rate_limit_entry));
  }
}

LocalRateLimitFilter::LocalRateLimitFilter(const RateLimitConfig& config,
                                           Server::Configuration::FactoryContext& context)
    : dispatcher_(context.mainThreadDispatcher()),
      stats_(generateStats(config.stat_prefix(), context.scope())),
      rate_limits_(config.rate_limits()),
      rate_limiter_(new LocalRateLimiterImpl(rate_limits_, dispatcher_)) {}

LocalRateLimitStats LocalRateLimitFilter::generateStats(const std::string& prefix,
                                                        Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".local_rate_limit";
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

bool LocalRateLimiterImpl::requestAllowed(const Request& request) {
  // The more specific rate limit conditions are the first priority
  std::vector<RateLimit::LocalDescriptor> descriptors;
  for (const auto& rate_limit_entry : rate_limit_entries_) {
    if (rate_limit_entry->match_->match(request)) {
      return rate_limit_entry->limiter_->requestAllowed(descriptors);
    }
  }
  // not match rate_limits
  return true;
}

void LocalRateLimitFilter::onDestroy() { cleanup(); }

void LocalRateLimitFilter::cleanup() {}

FilterStatus LocalRateLimitFilter::onStreamDecoded(Request& request) {
  if (!rate_limiter_->requestAllowed(request)) {
    ENVOY_LOG(debug, "generic_proxy local rate limit: id={}",
              request.getByKey("message_id").value());
    decoder_callbacks_->sendLocalReply(
        Status(StatusCode::kUnavailable,
               "generic_proxy local rate limit:request id has been rate limited"));
    stats_.rate_limited_.inc();

    return FilterStatus::StopIteration;
  }

  stats_.ok_.inc();
  ENVOY_LOG(debug, "generic_proxy local rate limit: onStreamDecoded");
  return FilterStatus::Continue;
}

} // namespace LocalRateLimit
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "envoy/event/dispatcher.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

Config::Config(
    const envoy::config::filter::network::local_rate_limit::v2alpha::LocalRateLimit& proto_config,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime)
    : fill_timer_(dispatcher.createTimer([this] { onFillTimer(); })),
      max_tokens_(proto_config.token_bucket().max_tokens()),
      tokens_per_fill_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.token_bucket(), tokens_per_fill, 1)),
      fill_interval_(PROTOBUF_GET_MS_REQUIRED(proto_config.token_bucket(), fill_interval)),
      enabled_(proto_config.enabled(), runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)), tokens_(max_tokens_) {
  fill_timer_->enableTimer(fill_interval_);
}

LocalRateLimitStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = "local_rate_limit." + prefix;
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

void Config::onFillTimer() {
  uint32_t new_tokens_value;
  {
    // TODO(mattklein123): Consider replacing with an atomic CAS loop.
    absl::MutexLock lock(&mutex_);
    new_tokens_value = std::min(max_tokens_, tokens_ + tokens_per_fill_);
    tokens_ = new_tokens_value;
  }

  ENVOY_LOG(trace, "local_rate_limit: fill tokens={}", new_tokens_value);
  fill_timer_->enableTimer(fill_interval_);
}

bool Config::canCreateConnection() {
  // TODO(mattklein123): Consider replacing with an atomic CAS loop.
  absl::MutexLock lock(&mutex_);
  if (tokens_ > 0) {
    tokens_--;
    return true;
  } else {
    return false;
  }
}

Network::FilterStatus Filter::onNewConnection() {
  if (!config_->enabled()) {
    ENVOY_CONN_LOG(trace, "local_rate_limit: runtime disabled", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  if (!config_->canCreateConnection()) {
    config_->stats().rate_limit_.inc();
    ENVOY_CONN_LOG(trace, "local_rate_limit: rate limiting connection",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

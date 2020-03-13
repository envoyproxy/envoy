#include "extensions/filters/network/ratelimit/ratelimit.h"

#include <cstdint>
#include <string>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/fmt.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

Config::Config(const envoy::extensions::filters::network::ratelimit::v3::RateLimit& config,
               Stats::Scope& scope, Runtime::Loader& runtime)
    : domain_(config.domain()), stats_(generateStats(config.stat_prefix(), scope)),
      runtime_(runtime), failure_mode_deny_(config.failure_mode_deny()) {
  for (const auto& descriptor : config.descriptors()) {
    RateLimit::Descriptor new_descriptor;
    for (const auto& entry : descriptor.entries()) {
      new_descriptor.entries_.push_back({entry.key(), entry.value()});
    }
    descriptors_.push_back(new_descriptor);
  }
}

InstanceStats Config::generateStats(const std::string& name, Stats::Scope& scope) {
  std::string final_prefix = fmt::format("ratelimit.{}.", name);
  return {ALL_TCP_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                   POOL_GAUGE_PREFIX(scope, final_prefix))};
}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool) {
  return status_ == Status::Calling ? Network::FilterStatus::StopIteration
                                    : Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  if (status_ == Status::NotStarted &&
      !config_->runtime().snapshot().featureEnabled("ratelimit.tcp_filter_enabled", 100)) {
    status_ = Status::Complete;
  }

  if (status_ == Status::NotStarted) {
    status_ = Status::Calling;
    config_->stats().active_.inc();
    config_->stats().total_.inc();
    calling_limit_ = true;
    client_->limit(*this, config_->domain(), config_->descriptors(), Tracing::NullSpan::instance());
    calling_limit_ = false;
  }

  return status_ == Status::Calling ? Network::FilterStatus::StopIteration
                                    : Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  // Make sure that any pending request in the client is cancelled. This will be NOP if the
  // request already completed.
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (status_ == Status::Calling) {
      client_->cancel();
      config_->stats().active_.dec();
    }
  }
}

void Filter::complete(Filters::Common::RateLimit::LimitStatus status, Http::ResponseHeaderMapPtr&&,
                      Http::RequestHeaderMapPtr&&) {
  status_ = Status::Complete;
  config_->stats().active_.dec();

  switch (status) {
  case Filters::Common::RateLimit::LimitStatus::OK:
    config_->stats().ok_.inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::Error:
    config_->stats().error_.inc();
    break;
  case Filters::Common::RateLimit::LimitStatus::OverLimit:
    config_->stats().over_limit_.inc();
    break;
  }

  if (status == Filters::Common::RateLimit::LimitStatus::OverLimit &&
      config_->runtime().snapshot().featureEnabled("ratelimit.tcp_filter_enforcing", 100)) {
    config_->stats().cx_closed_.inc();
    filter_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  } else if (status == Filters::Common::RateLimit::LimitStatus::Error) {
    if (config_->failureModeAllow()) {
      config_->stats().failure_mode_allowed_.inc();
      if (!calling_limit_) {
        filter_callbacks_->continueReading();
      }
    } else {
      config_->stats().cx_closed_.inc();
      filter_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
  } else {
    // We can get completion inline, so only call continue if that isn't happening.
    if (!calling_limit_) {
      filter_callbacks_->continueReading();
    }
  }
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

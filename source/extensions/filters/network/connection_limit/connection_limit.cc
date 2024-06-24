#include "source/extensions/filters/network/connection_limit/connection_limit.h"

#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitFilter {

Config::Config(
    const envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit& proto_config,
    Stats::Scope& scope, Runtime::Loader& runtime)
    : enabled_(proto_config.runtime_enabled(), runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)),
      max_connections_(PROTOBUF_GET_WRAPPED_REQUIRED(proto_config, max_connections)),
      connections_(0), delay_(PROTOBUF_GET_OPTIONAL_MS(proto_config, delay)) {}

ConnectionLimitStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = "connection_limit." + prefix;
  return {ALL_CONNECTION_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                     POOL_GAUGE_PREFIX(scope, final_prefix))};
}

bool Config::incrementConnectionWithinLimit() {
  auto conns = connections_.load(std::memory_order_relaxed);
  while (conns < max_connections_) {
    // Testing hook.
    synchronizer_.syncPoint("increment_pre_cas");

    if (connections_.compare_exchange_weak(conns, conns + 1, std::memory_order_release,
                                           std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void Config::incrementConnection() { connections_++; }

void Config::decrementConnection() {
  ASSERT(connections_ > 0);
  connections_--;
}

void Filter::resetTimerState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool) {
  if (is_rejected_) {
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  if (!config_->enabled()) {
    ENVOY_CONN_LOG(trace, "connection_limit: runtime disabled", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  config_->stats().active_connections_.inc();

  if (!config_->incrementConnectionWithinLimit()) {
    config_->stats().limited_connections_.inc();
    ENVOY_CONN_LOG(trace, "connection_limit: connection limiting connection",
                   read_callbacks_->connection());

    // Set is_rejected_ is true, so that onData() will return StopIteration during the delay time.
    is_rejected_ = true;
    // The close() will trigger onEvent() with close event, increment the active connections count.
    config_->incrementConnection();

    // Delay rejection provides a better DoS protection for Envoy.
    absl::optional<std::chrono::milliseconds> duration = config_->delay();
    if (duration.has_value() && duration.value() > std::chrono::milliseconds(0)) {
      delay_timer_ = read_callbacks_->connection().dispatcher().createTimer([this]() -> void {
        read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                            "over_connection_limit");
      });
      delay_timer_->enableTimer(duration.value());
    } else {
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                          "over_connection_limit");
    }
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    resetTimerState();
    config_->decrementConnection();
    config_->stats().active_connections_.dec();
  }
}

} // namespace ConnectionLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

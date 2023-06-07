#include "source/extensions/filters/network/connection_limit_per_client/connection_limit_per_client.h"

#include "envoy/extensions/filters/network/connection_limit_per_client/v3/connection_limit_per_client.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitPerClientFilter {

Config::Config(
    const envoy::extensions::filters::network::connection_limit_per_client::v3::ConnectionLimitPerClient& proto_config,
    Stats::Scope& scope, Runtime::Loader& runtime)
    : enabled_(proto_config.runtime_enabled(), runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)),
      max_connections_(PROTOBUF_GET_WRAPPED_REQUIRED(proto_config, max_connections)),
      delay_(PROTOBUF_GET_OPTIONAL_MS(proto_config, delay)) {}

ConnectionLimitPerClientStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = "connection_limit_per_client." + prefix;
  return {ALL_CONNECTION_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                     POOL_GAUGE_PREFIX(scope, final_prefix))};
}

bool Config::incrementConnectionWithinLimit(const std::string& client_address) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& conns = connections_[client_address];
  if (conns < max_connections_) {
    // Testing hook.
    synchronizer_.syncPoint("increment_pre_cas");

    conns++;
    return true;
  }
  return false;
}

void Config::incrementConnection(const std::string& client_address) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_[client_address]++;
}

void Config::decrementConnection(const std::string& client_address) {
  std::lock_guard<std::mutex> lock(mutex_);

  // TODO: check key exists before asserting > 0
  ASSERT(connections_[client_address] > 0);
  connections_[client_address]--;
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
    ENVOY_CONN_LOG(trace, "connection_limit_per_client: runtime disabled", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }
  std::string client_address = read_callbacks_->connection().connectionInfoProvider().remoteAddress()->asString();
  std::size_t pos = client_address.find_last_of(':');
  client_address = client_address.substr(0, pos);

  config_->stats().active_connections_.inc();

  if (!config_->incrementConnectionWithinLimit(client_address)) {
    config_->stats().limited_connections_.inc();
    ENVOY_CONN_LOG(info, "connection_limit_per_client: limiting connection (from: {})",
                   read_callbacks_->connection(), client_address);

    // Set is_rejected_ is true, so that onData() will return StopIteration during the delay time.
    is_rejected_ = true;
    // The close() will trigger onEvent() with close event, increment the active connections count.
    config_->incrementConnection(client_address);

    // Delay rejection provides a better DoS protection for Envoy.
    absl::optional<std::chrono::milliseconds> duration = config_->delay();
    if (duration.has_value() && duration.value() > std::chrono::milliseconds(0)) {
      delay_timer_ = read_callbacks_->connection().dispatcher().createTimer([this]() -> void {
        resetTimerState();
        read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      });
      delay_timer_->enableTimer(duration.value());
    } else {
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    return Network::FilterStatus::StopIteration;
  }

  ENVOY_CONN_LOG(info, "connection_limit_per_client: allowing connection (from: {})",
                   read_callbacks_->connection(), client_address);

  return Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    resetTimerState();

    std::string client_address = read_callbacks_->connection().connectionInfoProvider().remoteAddress()->asString();
    std::size_t pos = client_address.find_last_of(':');
    client_address = client_address.substr(0, pos);
    config_->decrementConnection(client_address);
    config_->stats().active_connections_.dec();

    ENVOY_CONN_LOG(info, "connection_limit_per_client: closing connection (from: {})",
                   read_callbacks_->connection(), client_address);
  }
}

} // namespace ConnectionLimitPerClientFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

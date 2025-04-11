#include "source/extensions/common/fluentd/fluentd_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Fluentd {

FluentdBase::FluentdBase(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                         Event::Dispatcher& dispatcher, const std::string& tag,
                         absl::optional<uint32_t> max_connect_attempts, Stats::Scope& parent_scope,
                         const std::string& stat_prefix, BackOffStrategyPtr backoff_strategy,
                         uint64_t buffer_flush_interval, uint64_t max_buffer_size)
    : tag_(tag), id_(dispatcher.name()), max_connect_attempts_(max_connect_attempts),
      stats_scope_(parent_scope.createScope(stat_prefix)), cluster_(cluster),
      backoff_strategy_(std::move(backoff_strategy)), client_(std::move(client)),
      buffer_flush_interval_msec_(buffer_flush_interval), max_buffer_size_bytes_(max_buffer_size),
      retry_timer_(dispatcher.createTimer([this]() { onBackoffCallback(); })),
      flush_timer_(dispatcher.createTimer([this]() {
        flush();
        flush_timer_->enableTimer(buffer_flush_interval_msec_);
      })),
      fluentd_stats_({FLUENTD_STATS(POOL_COUNTER(*stats_scope_), POOL_GAUGE(*stats_scope_))}) {
  client_->setAsyncTcpClientCallbacks(*this);
  flush_timer_->enableTimer(buffer_flush_interval_msec_);
}

void FluentdBase::onEvent(Network::ConnectionEvent event) {
  connecting_ = false;

  if (event == Network::ConnectionEvent::Connected) {
    backoff_strategy_->reset();
    retry_timer_->disableTimer();
    flush();
  } else if (event == Network::ConnectionEvent::LocalClose ||
             event == Network::ConnectionEvent::RemoteClose) {
    ENVOY_LOG(debug, "upstream connection was closed");
    fluentd_stats_.connections_closed_.inc();
    maybeReconnect();
  }
}

void FluentdBase::log(EntryPtr&& entry) {
  if (disconnected_ || approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    fluentd_stats_.entries_lost_.inc();
    // We will lose the data deliberately so the buffer doesn't grow infinitely.
    return;
  }

  approximate_message_size_bytes_ +=
      sizeof(entry->time_) + entry->record_.size() + entry->map_record_.size();
  entries_.push_back(std::move(entry));
  fluentd_stats_.entries_buffered_.inc();
  if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    // If we exceeded the buffer limit, immediately flush the logs instead of waiting for
    // the next flush interval, to allow new logs to be buffered.
    flush();
  }
}

void FluentdBase::flush() {
  ASSERT(!disconnected_);

  if (entries_.empty() || connecting_) {
    return; // Nothing to send or waiting for an upstream connection
  }

  if (!client_->connected()) {
    connect();
    return;
  }

  // Pack the message using the derived class implementation
  MessagePackBuffer buffer;
  MessagePackPacker packer(buffer);
  packMessage(packer);
  Buffer::OwnedImpl data(buffer.data(), buffer.size());

  client_->write(data, false);
  fluentd_stats_.events_sent_.inc();
  clearBuffer();
}

void FluentdBase::connect() {
  connect_attempts_++;
  if (!client_->connect()) {
    ENVOY_LOG(debug, "no healthy upstream");
    maybeReconnect();
    return;
  }

  connecting_ = true;
}

void FluentdBase::maybeReconnect() {
  if (max_connect_attempts_.has_value() && connect_attempts_ >= max_connect_attempts_) {
    ENVOY_LOG(debug, "max connection attempts reached");
    cluster_.info()->trafficStats()->upstream_cx_connect_attempts_exceeded_.inc();
    setDisconnected();
    return;
  }

  uint64_t next_backoff_ms = backoff_strategy_->nextBackOffMs();
  retry_timer_->enableTimer(std::chrono::milliseconds(next_backoff_ms));
  ENVOY_LOG(debug, "reconnect attempt scheduled for {} ms", next_backoff_ms);
}

void FluentdBase::onBackoffCallback() {
  fluentd_stats_.reconnect_attempts_.inc();
  this->connect();
}

void FluentdBase::setDisconnected() {
  disconnected_ = true;
  clearBuffer();
  ASSERT(flush_timer_ != nullptr);
  flush_timer_->disableTimer();
}

void FluentdBase::clearBuffer() {
  entries_.clear();
  approximate_message_size_bytes_ = 0;
}

} // namespace Fluentd
} // namespace Common
} // namespace Extensions
} // namespace Envoy

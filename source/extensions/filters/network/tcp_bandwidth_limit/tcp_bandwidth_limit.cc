#include "source/extensions/filters/network/tcp_bandwidth_limit/tcp_bandwidth_limit.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {
namespace {

constexpr uint64_t kiloBytesToBytes(uint64_t val) { return val * 1024; }
constexpr int64_t RateUpdateIntervalMs = 1000;
constexpr uint64_t MillisecondsPerSecond = 1000;

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit& config,
    Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source)
    : runtime_(runtime), time_source_(time_source),
      read_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, read_limit_kbps, 0)),
      write_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, write_limit_kbps, 0)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, fill_interval, 50))), // Default 50ms
      enabled_(config.runtime_enabled(), runtime),
      stats_(generateStats(config.stat_prefix(), scope)),
      // The token bucket is configured with a max token count of the number of
      // bytes per second, and refills at the same rate, so that we have a per
      // second limit which refills gradually over the fill interval.
      read_token_bucket_(config.has_read_limit_kbps()
                             ? std::make_shared<SharedTokenBucketImpl>(
                                   kiloBytesToBytes(read_limit_kbps_), time_source_,
                                   static_cast<double>(kiloBytesToBytes(read_limit_kbps_)))
                             : nullptr),
      write_token_bucket_(config.has_write_limit_kbps()
                              ? std::make_shared<SharedTokenBucketImpl>(
                                    kiloBytesToBytes(write_limit_kbps_), time_source_,
                                    static_cast<double>(kiloBytesToBytes(write_limit_kbps_)))
                              : nullptr) {}

TcpBandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".tcp_bandwidth_limit";
  return {ALL_TCP_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                        POOL_GAUGE_PREFIX(scope, final_prefix))};
}

TcpBandwidthLimitFilter::TcpBandwidthLimitFilter(FilterConfigSharedPtr config)
    : config_(config), read_buffer_([this]() { onReadBufferLowWatermark(); },
                                    [this]() { onReadBufferHighWatermark(); }, []() -> void {}),
      write_buffer_([this]() { onWriteBufferLowWatermark(); },
                    [this]() { onWriteBufferHighWatermark(); }, []() -> void {}),
      last_read_rate_update_(config->timeSource().monotonicTime()),
      last_write_rate_update_(config->timeSource().monotonicTime()) {}

TcpBandwidthLimitFilter::~TcpBandwidthLimitFilter() {
  if (read_timer_) {
    read_timer_->disableTimer();
    read_timer_.reset();
  }
  if (write_timer_) {
    write_timer_->disableTimer();
    write_timer_.reset();
  }
}

Network::FilterStatus TcpBandwidthLimitFilter::onData(Buffer::Instance& data, bool end_stream) {
  if (!config_->enabled() || !config_->hasReadLimit()) {
    return Network::FilterStatus::Continue;
  }

  config_->stats().read_enabled_.inc();

  // If there's already buffered data, we must buffer new data too to preserve byte ordering.
  if (read_buffer_.length() > 0) {
    config_->stats().read_throttled_.inc();
    read_end_stream_ = end_stream;
    read_buffer_.move(data);
    config_->stats().read_bytes_buffered_.set(read_buffer_.length());
    return Network::FilterStatus::StopIteration;
  }

  uint64_t data_size = data.length();
  uint64_t consumed = config_->readTokenBucket()->consume(data_size, true);

  if (consumed < data_size) {
    config_->stats().read_throttled_.inc();

    if (consumed > 0) {
      Buffer::OwnedImpl passthrough;
      passthrough.move(data, consumed);
      read_callbacks_->injectReadDataToFilterChain(passthrough, false);
      updateReadRate(consumed);
    }

    read_end_stream_ = end_stream;
    read_buffer_.move(data);
    config_->stats().read_bytes_buffered_.set(read_buffer_.length());

    if (!read_timer_) {
      read_timer_ =
          read_callbacks_->connection().dispatcher().createTimer([this]() { onReadTokenTimer(); });
      read_timer_->enableTimer(config_->fillInterval());
    }

    return Network::FilterStatus::StopIteration;
  }

  updateReadRate(data_size);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus TcpBandwidthLimitFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  if (!config_->enabled() || !config_->hasWriteLimit()) {
    return Network::FilterStatus::Continue;
  }

  config_->stats().write_enabled_.inc();

  // If there's already buffered data, we must buffer new data too to preserve byte ordering.
  if (write_buffer_.length() > 0) {
    config_->stats().write_throttled_.inc();
    write_end_stream_ = end_stream;
    write_buffer_.move(data);
    config_->stats().write_bytes_buffered_.set(write_buffer_.length());
    return Network::FilterStatus::StopIteration;
  }

  uint64_t data_size = data.length();
  uint64_t consumed = config_->writeTokenBucket()->consume(data_size, true);

  if (consumed < data_size) {
    config_->stats().write_throttled_.inc();

    if (consumed > 0) {
      Buffer::OwnedImpl to_send;
      to_send.move(data, consumed);
      write_callbacks_->injectWriteDataToFilterChain(to_send, false);
      updateWriteRate(consumed);
    }

    write_end_stream_ = end_stream;
    write_buffer_.move(data);
    config_->stats().write_bytes_buffered_.set(write_buffer_.length());

    if (!write_timer_) {
      write_timer_ =
          read_callbacks_->connection().dispatcher().createTimer([this]() { onWriteTokenTimer(); });
      write_timer_->enableTimer(config_->fillInterval());
    }

    return Network::FilterStatus::StopIteration;
  }

  updateWriteRate(data_size);
  return Network::FilterStatus::Continue;
}

void TcpBandwidthLimitFilter::onReadTokenTimer() {
  processBufferedReadData();

  if (read_buffer_.length() > 0) {
    read_timer_->enableTimer(config_->fillInterval());
  } else {
    read_timer_.reset();
  }
}

void TcpBandwidthLimitFilter::onWriteTokenTimer() {
  processBufferedWriteData();

  if (write_buffer_.length() > 0) {
    write_timer_->enableTimer(config_->fillInterval());
  } else {
    write_timer_.reset();
  }
}

void TcpBandwidthLimitFilter::onReadBufferHighWatermark() {
  read_callbacks_->connection().readDisable(true);
}

void TcpBandwidthLimitFilter::onReadBufferLowWatermark() {
  read_callbacks_->connection().readDisable(false);
}

void TcpBandwidthLimitFilter::onWriteBufferHighWatermark() {
  write_callbacks_->onAboveWriteBufferHighWatermark();
}

void TcpBandwidthLimitFilter::onWriteBufferLowWatermark() {
  write_callbacks_->onBelowWriteBufferLowWatermark();
}

void TcpBandwidthLimitFilter::processBufferedReadData() {
  if (read_buffer_.length() == 0 || !config_->readTokenBucket()) {
    return;
  }

  uint64_t buffer_size = read_buffer_.length();
  uint64_t consumed = config_->readTokenBucket()->consume(buffer_size, true);

  if (consumed > 0) {
    Buffer::OwnedImpl data_to_send;
    data_to_send.move(read_buffer_, consumed);
    const bool end_stream = read_end_stream_ && read_buffer_.length() == 0;
    read_callbacks_->injectReadDataToFilterChain(data_to_send, end_stream);
    updateReadRate(consumed);
    config_->stats().read_bytes_buffered_.set(read_buffer_.length());
  }
}

void TcpBandwidthLimitFilter::processBufferedWriteData() {
  if (write_buffer_.length() == 0 || !config_->writeTokenBucket()) {
    return;
  }

  uint64_t buffer_size = write_buffer_.length();
  uint64_t consumed = config_->writeTokenBucket()->consume(buffer_size, true);

  if (consumed > 0) {
    Buffer::OwnedImpl data_to_send;
    data_to_send.move(write_buffer_, consumed);
    const bool end_stream = write_end_stream_ && write_buffer_.length() == 0;
    write_callbacks_->injectWriteDataToFilterChain(data_to_send, end_stream);
    updateWriteRate(consumed);
    config_->stats().write_bytes_buffered_.set(write_buffer_.length());
  }
}

void TcpBandwidthLimitFilter::updateReadRate(uint64_t bytes) {
  config_->stats().read_total_bytes_.add(bytes);
  read_bytes_since_last_rate_ += bytes;
  const auto now = config_->timeSource().monotonicTime();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_read_rate_update_);
  if (elapsed.count() >= RateUpdateIntervalMs) {
    const uint64_t rate = (read_bytes_since_last_rate_ * MillisecondsPerSecond) / elapsed.count();
    config_->stats().read_rate_bps_.set(rate);
    read_bytes_since_last_rate_ = 0;
    last_read_rate_update_ = now;
  }
}

void TcpBandwidthLimitFilter::updateWriteRate(uint64_t bytes) {
  config_->stats().write_total_bytes_.add(bytes);
  write_bytes_since_last_rate_ += bytes;
  const auto now = config_->timeSource().monotonicTime();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_write_rate_update_);
  if (elapsed.count() >= RateUpdateIntervalMs) {
    const uint64_t rate = (write_bytes_since_last_rate_ * MillisecondsPerSecond) / elapsed.count();
    config_->stats().write_rate_bps_.set(rate);
    write_bytes_since_last_rate_ = 0;
    last_write_rate_update_ = now;
  }
}

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

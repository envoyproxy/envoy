#include "source/extensions/filters/network/tcp_bandwidth_limit/tcp_bandwidth_limit.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit& config,
    Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source)
    : runtime_(runtime), time_source_(time_source),
      has_download_limit_(config.has_download_limit_kbps()),
      has_upload_limit_(config.has_upload_limit_kbps()),
      download_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, download_limit_kbps, 0)),
      upload_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, upload_limit_kbps, 0)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, fill_interval, 50))), // Default 50ms
      enabled_(config.runtime_enabled(), runtime),
      stats_(generateStats(config.stat_prefix(), scope)) {

  if (has_download_limit_) {
    // The token bucket is configured with a max token count of the number of
    // bytes per second, and refills at the same rate, so that we have a per
    // second limit which refills gradually over the fill interval.
    uint64_t limit_bytes_per_sec = kiloBytesToBytes(download_limit_kbps_);
    download_token_bucket_ = std::make_shared<SharedTokenBucketImpl>(
        limit_bytes_per_sec, time_source_, static_cast<double>(limit_bytes_per_sec));
  }

  if (has_upload_limit_) {
    uint64_t limit_bytes_per_sec = kiloBytesToBytes(upload_limit_kbps_);
    upload_token_bucket_ = std::make_shared<SharedTokenBucketImpl>(
        limit_bytes_per_sec, time_source_, static_cast<double>(limit_bytes_per_sec));
  }
}

TcpBandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".tcp_bandwidth_limit";
  return {ALL_TCP_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

TcpBandwidthLimitFilter::TcpBandwidthLimitFilter(FilterConfigSharedPtr config,
                                                 Event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {}

Network::FilterStatus TcpBandwidthLimitFilter::onData(Buffer::Instance& data, bool) {
  if (!config_->enabled() || !config_->hasDownloadLimit()) {
    return Network::FilterStatus::Continue;
  }

  config_->stats().download_enabled_.inc();

  uint64_t data_size = data.length();
  uint64_t consumed = config_->downloadTokenBucket()->consume(data_size, true);

  if (consumed < data_size) {
    config_->stats().download_throttled_.inc();

    if (consumed > 0) {
      Buffer::OwnedImpl passthrough;
      passthrough.move(data, consumed);
      read_callbacks_->injectReadDataToFilterChain(passthrough, false);
    }

    download_buffer_.move(data);

    if (!read_disabled_) {
      read_callbacks_->connection().readDisable(true);
      read_disabled_ = true;
    }

    if (!download_timer_) {
      download_timer_ = dispatcher_.createTimer([this]() { onDownloadTokenTimer(); });
      download_timer_->enableTimer(config_->fillInterval());
    }

    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus TcpBandwidthLimitFilter::onWrite(Buffer::Instance& data, bool) {
  if (!config_->enabled() || !config_->hasUploadLimit()) {
    return Network::FilterStatus::Continue;
  }

  config_->stats().upload_enabled_.inc();

  uint64_t data_size = data.length();
  uint64_t consumed = config_->uploadTokenBucket()->consume(data_size, true);

  if (consumed < data_size) {
    config_->stats().upload_throttled_.inc();

    if (consumed > 0) {
      Buffer::OwnedImpl to_send;
      to_send.move(data, consumed);
      write_callbacks_->connection().write(to_send, false);
    }

    upload_buffer_.move(data);

    if (!upload_timer_) {
      upload_timer_ = dispatcher_.createTimer([this]() { onUploadTokenTimer(); });
      upload_timer_->enableTimer(config_->fillInterval());
    }

    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

void TcpBandwidthLimitFilter::onDownloadTokenTimer() {
  processBufferedDownloadData();

  if (download_buffer_.length() > 0) {
    download_timer_->enableTimer(config_->fillInterval());
  } else {
    download_timer_.reset();

    if (read_disabled_) {
      read_callbacks_->connection().readDisable(false);
      read_disabled_ = false;
    }
  }
}

void TcpBandwidthLimitFilter::onUploadTokenTimer() {
  processBufferedUploadData();

  if (upload_buffer_.length() > 0) {
    upload_timer_->enableTimer(config_->fillInterval());
  } else {
    upload_timer_.reset();
  }
}

void TcpBandwidthLimitFilter::processBufferedDownloadData() {
  if (download_buffer_.length() == 0 || !config_->downloadTokenBucket()) {
    return;
  }

  uint64_t buffer_size = download_buffer_.length();
  uint64_t consumed = config_->downloadTokenBucket()->consume(buffer_size, true);

  if (consumed > 0) {
    Buffer::OwnedImpl data_to_send;
    data_to_send.move(download_buffer_, consumed);
    read_callbacks_->injectReadDataToFilterChain(data_to_send, false);
  }
}

void TcpBandwidthLimitFilter::processBufferedUploadData() {
  if (upload_buffer_.length() == 0 || !config_->uploadTokenBucket()) {
    return;
  }

  uint64_t buffer_size = upload_buffer_.length();
  uint64_t consumed = config_->uploadTokenBucket()->consume(buffer_size, true);

  if (consumed > 0) {
    Buffer::OwnedImpl data_to_send;
    data_to_send.move(upload_buffer_, consumed);
    write_callbacks_->connection().write(data_to_send, false);
  }
}

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

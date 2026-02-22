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
      download_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, download_limit_kbps, 0)),
      upload_limit_kbps_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, upload_limit_kbps, 0)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, fill_interval, 50))), // Default 50ms
      enabled_(config.runtime_enabled(), runtime),
      stats_(generateStats(config.stat_prefix(), scope)),
      // The token bucket is configured with a max token count of the number of
      // bytes per second, and refills at the same rate, so that we have a per
      // second limit which refills gradually over the fill interval.
      download_token_bucket_(config.has_download_limit_kbps()
                                 ? std::make_shared<SharedTokenBucketImpl>(
                                       kiloBytesToBytes(download_limit_kbps_), time_source_,
                                       static_cast<double>(kiloBytesToBytes(download_limit_kbps_)))
                                 : nullptr),
      upload_token_bucket_(config.has_upload_limit_kbps()
                               ? std::make_shared<SharedTokenBucketImpl>(
                                     kiloBytesToBytes(upload_limit_kbps_), time_source_,
                                     static_cast<double>(kiloBytesToBytes(upload_limit_kbps_)))
                               : nullptr) {}

TcpBandwidthLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".tcp_bandwidth_limit";
  return {ALL_TCP_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                        POOL_GAUGE_PREFIX(scope, final_prefix))};
}

TcpBandwidthLimitFilter::TcpBandwidthLimitFilter(FilterConfigSharedPtr config)
    : config_(config),
      download_buffer_([this]() { onDownloadBufferLowWatermark(); },
                       [this]() { onDownloadBufferHighWatermark(); }, []() -> void {}) {}

TcpBandwidthLimitFilter::~TcpBandwidthLimitFilter() {
  if (download_timer_) {
    download_timer_->disableTimer();
    download_timer_.reset();
  }
  if (upload_timer_) {
    upload_timer_->disableTimer();
    upload_timer_.reset();
  }
}

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
    config_->stats().download_bytes_buffered_.set(download_buffer_.length());

    if (!download_timer_) {
      download_timer_ = read_callbacks_->connection().dispatcher().createTimer(
          [this]() { onDownloadTokenTimer(); });
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
      write_callbacks_->injectWriteDataToFilterChain(to_send, false);
    }

    upload_buffer_.move(data);
    config_->stats().upload_bytes_buffered_.set(upload_buffer_.length());

    if (!upload_timer_) {
      upload_timer_ = read_callbacks_->connection().dispatcher().createTimer(
          [this]() { onUploadTokenTimer(); });
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

void TcpBandwidthLimitFilter::onDownloadBufferHighWatermark() {
  read_callbacks_->connection().readDisable(true);
}

void TcpBandwidthLimitFilter::onDownloadBufferLowWatermark() {
  read_callbacks_->connection().readDisable(false);
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
    config_->stats().download_bytes_buffered_.set(download_buffer_.length());
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
    write_callbacks_->injectWriteDataToFilterChain(data_to_send, false);
    config_->stats().upload_bytes_buffered_.set(upload_buffer_.length());
  }
}

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

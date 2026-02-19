#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/tcp_bandwidth_limit/v3/tcp_bandwidth_limit.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/logger.h"
#include "source/common/common/shared_token_bucket_impl.h"
#include "source/common/runtime/runtime_protos.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {

/**
 * All TCP bandwidth limit stats. @see stats_macros.h
 */
#define ALL_TCP_BANDWIDTH_LIMIT_STATS(COUNTER)                                                     \
  COUNTER(download_enabled)                                                                        \
  COUNTER(upload_enabled)                                                                          \
  COUNTER(download_throttled)                                                                      \
  COUNTER(upload_throttled)

/**
 * Struct definition for all TCP bandwidth limit stats. @see stats_macros.h
 */
struct TcpBandwidthLimitStats {
  ALL_TCP_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the TCP bandwidth limit filter.
 */
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit& config,
      Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source);

  Runtime::Loader& runtime() { return runtime_; }
  TcpBandwidthLimitStats& stats() { return stats_; }
  TimeSource& timeSource() { return time_source_; }

  bool hasDownloadLimit() const { return download_token_bucket_ != nullptr; }
  bool hasUploadLimit() const { return upload_token_bucket_ != nullptr; }
  uint64_t downloadLimit() const { return download_limit_kbps_; }
  uint64_t uploadLimit() const { return upload_limit_kbps_; }
  bool enabled() const { return enabled_.enabled(); }
  const std::shared_ptr<SharedTokenBucketImpl>& downloadTokenBucket() const {
    return download_token_bucket_;
  }
  const std::shared_ptr<SharedTokenBucketImpl>& uploadTokenBucket() const {
    return upload_token_bucket_;
  }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

private:
  static TcpBandwidthLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);
  static constexpr uint64_t kiloBytesToBytes(uint64_t kilobytes) { return kilobytes * 1024; }

  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  const uint64_t download_limit_kbps_;
  const uint64_t upload_limit_kbps_;
  const std::chrono::milliseconds fill_interval_;
  const Runtime::FeatureFlag enabled_;
  TcpBandwidthLimitStats stats_;
  std::shared_ptr<SharedTokenBucketImpl> download_token_bucket_;
  std::shared_ptr<SharedTokenBucketImpl> upload_token_bucket_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * TCP bandwidth limit filter. Implements rate limiting for TCP connections.
 */
class TcpBandwidthLimitFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  TcpBandwidthLimitFilter(FilterConfigSharedPtr config, Event::Dispatcher& dispatcher);
  ~TcpBandwidthLimitFilter() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    download_buffer_.setWatermarks(callbacks.connection().bufferLimit());
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  void onDownloadTokenTimer();
  void onUploadTokenTimer();

private:
  friend class TcpBandwidthLimitFilterTest;

  void processBufferedDownloadData();
  void processBufferedUploadData();
  void onDownloadBufferLowWatermark();
  void onDownloadBufferHighWatermark();

  FilterConfigSharedPtr config_;
  Event::Dispatcher& dispatcher_;

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  // Buffered data waiting for tokens
  Buffer::WatermarkBuffer download_buffer_;
  Buffer::OwnedImpl upload_buffer_;

  // Timers for processing buffered data
  Event::TimerPtr download_timer_;
  Event::TimerPtr upload_timer_;
};

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

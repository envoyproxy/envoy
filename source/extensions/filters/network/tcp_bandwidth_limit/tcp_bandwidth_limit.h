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
#define ALL_TCP_BANDWIDTH_LIMIT_STATS(COUNTER, GAUGE)                                              \
  COUNTER(read_enabled)                                                                            \
  COUNTER(write_enabled)                                                                           \
  COUNTER(read_throttled)                                                                          \
  COUNTER(write_throttled)                                                                         \
  COUNTER(read_total_bytes)                                                                        \
  COUNTER(write_total_bytes)                                                                       \
  GAUGE(read_bytes_buffered, Accumulate)                                                           \
  GAUGE(write_bytes_buffered, Accumulate)                                                          \
  GAUGE(read_rate_bps, NeverImport)                                                                \
  GAUGE(write_rate_bps, NeverImport)

/**
 * Struct definition for all TCP bandwidth limit stats. @see stats_macros.h
 */
struct TcpBandwidthLimitStats {
  ALL_TCP_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
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

  bool hasReadLimit() const { return read_token_bucket_ != nullptr; }
  bool hasWriteLimit() const { return write_token_bucket_ != nullptr; }
  uint64_t readLimit() const { return read_limit_kbps_; }
  uint64_t writeLimit() const { return write_limit_kbps_; }
  bool enabled() const { return enabled_.enabled(); }
  const std::shared_ptr<SharedTokenBucketImpl>& readTokenBucket() const {
    return read_token_bucket_;
  }
  const std::shared_ptr<SharedTokenBucketImpl>& writeTokenBucket() const {
    return write_token_bucket_;
  }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

private:
  static TcpBandwidthLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  const uint64_t read_limit_kbps_;
  const uint64_t write_limit_kbps_;
  const std::chrono::milliseconds fill_interval_;
  const Runtime::FeatureFlag enabled_;
  TcpBandwidthLimitStats stats_;
  std::shared_ptr<SharedTokenBucketImpl> read_token_bucket_;
  std::shared_ptr<SharedTokenBucketImpl> write_token_bucket_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * TCP bandwidth limit filter. Implements rate limiting for TCP connections.
 */
class TcpBandwidthLimitFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit TcpBandwidthLimitFilter(FilterConfigSharedPtr config);
  ~TcpBandwidthLimitFilter() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    write_buffer_.setWatermarks(callbacks.connection().bufferLimit());
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
    read_buffer_.setWatermarks(callbacks.connection().bufferLimit());
  }

  void onReadTokenTimer();
  void onWriteTokenTimer();

private:
  friend class TcpBandwidthLimitFilterTest;

  void processBufferedReadData();
  void processBufferedWriteData();
  void onReadBufferLowWatermark();
  void onReadBufferHighWatermark();
  void onWriteBufferLowWatermark();
  void onWriteBufferHighWatermark();
  void updateReadRate(uint64_t bytes);
  void updateWriteRate(uint64_t bytes);

  FilterConfigSharedPtr config_;

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  // Buffered data waiting for tokens
  Buffer::WatermarkBuffer read_buffer_;
  Buffer::WatermarkBuffer write_buffer_;

  // Whether end_stream was observed while buffering data
  bool read_end_stream_{false};
  bool write_end_stream_{false};

  // Bytes sent since last rate update
  uint64_t read_bytes_since_last_rate_{0};
  uint64_t write_bytes_since_last_rate_{0};
  MonotonicTime last_read_rate_update_;
  MonotonicTime last_write_rate_update_;

  // Timers for processing buffered data
  Event::TimerPtr read_timer_;
  Event::TimerPtr write_timer_;
};

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

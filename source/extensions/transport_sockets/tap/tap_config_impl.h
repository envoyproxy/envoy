#pragma once

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/transport.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/common/tap/tap_config_base.h"
#include "source/extensions/transport_sockets/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

class PerSocketTapperImpl : public PerSocketTapper {
public:
  PerSocketTapperImpl(
      SocketTapConfigSharedPtr config,
      const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig& tap_config,
      const TransportTapStats& stats, const Network::Connection& connection);

  // PerSocketTapper
  void closeSocket(Network::ConnectionEvent event) override;
  void onRead(const Buffer::Instance& data, uint32_t bytes_read) override;
  void onWrite(const Buffer::Instance& data, uint32_t bytes_written, bool end_stream) override;

private:
  void initEvent(envoy::data::tap::v3::SocketEvent&);
  void initStreamingEvent(envoy::data::tap::v3::SocketEvent&, uint64_t);
  void makeStreamedTraceIfNeeded() {
    if (streamed_trace_ == nullptr) {
      streamed_trace_ = Extensions::Common::Tap::makeTraceWrapper();
      streamed_trace_->mutable_socket_streamed_trace_segment()->set_trace_id(connection_.id());
    }
  }
  void fillConnectionInfo(envoy::data::tap::v3::Connection& connection);
  void makeBufferedTraceIfNeeded() {
    if (buffered_trace_ == nullptr) {
      buffered_trace_ = Extensions::Common::Tap::makeTraceWrapper();
      buffered_trace_->mutable_socket_buffered_trace()->set_trace_id(connection_.id());
    }
  }
  Extensions::Common::Tap::TraceWrapperPtr makeTraceSegment() {
    Extensions::Common::Tap::TraceWrapperPtr trace = Extensions::Common::Tap::makeTraceWrapper();
    trace->mutable_socket_streamed_trace_segment()->set_trace_id(connection_.id());
    return trace;
  }
  void pegSubmitCounter(const bool is_streaming);
  bool shouldSendStreamedMsgByConfiguredSize() const;
  bool shouldSubmitStreamedDataPerConfiguredSizeByAgedDuration() const;
  void submitStreamedDataPerConfiguredSize();
  void handleSendingStreamTappedMsgPerConfigSize(const Buffer::Instance& data,
                                                 const uint32_t total_bytes, const bool is_read,
                                                 const bool is_end_stream);
  // This is the default value for min buffered bytes.
  // (This means that per transport socket buffer trace, the minimum amount
  // which triggering to send the tapped messages size is 9 bytes).
  static constexpr uint32_t DefaultMinBufferedBytes = 9;
  // It isn't easy to meet data submit threshold when the configured byte size is too large
  // and the tapped data volume is low, therefore, set below buffer aged duration (seconds)
  // to make sure that the tapped data is submitted in time.
  static constexpr uint32_t DefaultBufferedAgedDuration = 15;
  // The tapped data from Transport socket may be incomplete
  // for some protocols (e.g., HTTP/2 frames may span multiple reads/writes).
  // Add sequence number to allow the receiver to reconstruct byte order and
  // determine completeness, similar to TCP sequence numbers.
  uint64_t seq_num{};
  static constexpr uint64_t MaxSeqNum = std::numeric_limits<uint64_t>::max();
  SocketTapConfigSharedPtr config_;
  Extensions::Common::Tap::PerTapSinkHandleManagerPtr sink_handle_;
  const Network::Connection& connection_;
  Extensions::Common::Tap::Matcher::MatchStatusVector statuses_;
  // Must be a shared_ptr because of submitTrace().
  Extensions::Common::Tap::TraceWrapperPtr buffered_trace_;
  uint32_t rx_bytes_buffered_{};
  uint32_t tx_bytes_buffered_{};
  const bool should_output_conn_info_per_event_{false};
  uint32_t current_streamed_rx_tx_bytes_{0};
  Extensions::Common::Tap::TraceWrapperPtr streamed_trace_{nullptr};
  const TransportTapStats stats_;
};

class SocketTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                            public SocketTapConfig,
                            public std::enable_shared_from_this<SocketTapConfigImpl> {
public:
  SocketTapConfigImpl(const envoy::config::tap::v3::TapConfig& proto_config,
                      Extensions::Common::Tap::Sink* admin_streamer, TimeSource& time_system,
                      Server::Configuration::TransportSocketFactoryContext& context)
      : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config), admin_streamer,
                                                   context),
        time_source_(time_system) {}

  // SocketTapConfig
  PerSocketTapperPtr createPerSocketTapper(
      const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig& tap_config,
      const TransportTapStats& stats, const Network::Connection& connection) override {
    return std::make_unique<PerSocketTapperImpl>(shared_from_this(), tap_config, stats, connection);
  }
  TimeSource& timeSource() const override { return time_source_; }

private:
  TimeSource& time_source_;

  friend class PerSocketTapperImpl;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

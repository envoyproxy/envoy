#include "source/extensions/transport_sockets/tap/tap_config_impl.h"

#include "envoy/data/tap/v3/transport.pb.h"

#include "source/common/common/assert.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

namespace TapCommon = Extensions::Common::Tap;

PerSocketTapperImpl::PerSocketTapperImpl(
    SocketTapConfigSharedPtr config,
    const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig& tap_config,
    const TransportTapStats& stats, const Network::Connection& connection)
    : config_(std::move(config)),
      sink_handle_(config_->createPerTapSinkHandleManager(connection.id())),
      connection_(connection), statuses_(config_->createMatchStatusVector()),
      should_output_conn_info_per_event_(tap_config.set_connection_per_event()), stats_(stats) {
  config_->rootMatcher().onNewStream(statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    // TODO(mattklein123): For IP client connections, local address will not be populated until
    // connection. We should re-emit connection information after connection so the streaming
    // trace gets the local address.
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    fillConnectionInfo(*trace->mutable_socket_streamed_trace_segment()->mutable_connection());
    sink_handle_->submitTrace(std::move(trace));
  }
  if (PROTOBUF_GET_OPTIONAL_WRAPPED(tap_config, min_buffered_bytes) != absl::nullopt) {
    min_sending_buffered_bytes_ =
        std::max(tap_config.min_buffered_bytes().value(), DefaultMinBufferedBytes);
    should_sending_tapped_msg_on_configured_size_ = true;
  }
}

void PerSocketTapperImpl::fillConnectionInfo(envoy::data::tap::v3::Connection& connection) {
  if (connection_.connectionInfoProvider().localAddress() != nullptr) {
    // Local address might not be populated before a client connection is connected.
    Network::Utility::addressToProtobufAddress(*connection_.connectionInfoProvider().localAddress(),
                                               *connection.mutable_local_address());
  }
  Network::Utility::addressToProtobufAddress(*connection_.connectionInfoProvider().remoteAddress(),
                                             *connection.mutable_remote_address());
}

void PerSocketTapperImpl::pegCloseSubmitCounter(const bool isStreaming) {
  if (isStreaming) {
    stats_.streamed_close_submit_.inc();
  } else {
    stats_.buffered_close_submit_.inc();
  }
}
void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initStreamingEvent(event);
    event.mutable_closed();
    sink_handle_->submitTrace(std::move(trace));
    pegCloseSubmitCounter(true);
  } else {
    makeBufferedTraceIfNeeded();
    fillConnectionInfo(*buffered_trace_->mutable_socket_buffered_trace()->mutable_connection());
    sink_handle_->submitTrace(std::move(buffered_trace_));
    pegCloseSubmitCounter(false);
  }

  // Here we explicitly reset the sink_handle_ to release any sink resources and force a flush
  // of any data (e.g., files). This is not explicitly needed in production, but is needed in
  // tests to avoid race conditions due to deferred deletion. We could also do this with a stat,
  // but this seems fine in general and is simpler.
  sink_handle_.reset();
}

void PerSocketTapperImpl::initEvent(envoy::data::tap::v3::SocketEvent& event) {
  event.mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->timeSource().systemTime().time_since_epoch())
          .count()));
}

void PerSocketTapperImpl::initStreamingEvent(envoy::data::tap::v3::SocketEvent& event) {
  initEvent(event);
  if (should_output_conn_info_per_event_) {
    fillConnectionInfo(*event.mutable_connection());
  }
}

void PerSocketTapperImpl::pegReadWriteSubmitCounter(const bool isStreaming, const bool isRead) {
  if (isStreaming) {
    if (isRead) {
      stats_.streamed_read_submit_.inc();
    } else {
      stats_.streamed_write_submit_.inc();
    }
  } else {
    if (isRead) {
      stats_.buffered_read_submit_.inc();
    } else {
      stats_.buffered_write_submit_.inc();
    }
  }
}

void PerSocketTapperImpl::handleSendingTappedMsgPerConfigSize(
    const Buffer::Instance& data, const uint32_t totalBytes,
    envoy::data::tap::v3::SocketEvent& event, const bool isRead) {

  uint32_t buffer_start_offset = 0;
  if (isRead) {
    buffer_start_offset = data.length() - totalBytes;
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(), totalBytes,
                                              data, buffer_start_offset, totalBytes);
    current_buffered_rx_tx_bytes_ += event.read().data().as_bytes().size();
  } else {
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_write()->mutable_data(), totalBytes,
                                              data, buffer_start_offset, totalBytes);
    current_buffered_rx_tx_bytes_ += event.write().data().as_bytes().size();
  }

  if (current_buffered_rx_tx_bytes_ >= min_sending_buffered_bytes_) {
    fillConnectionInfo(*buffered_trace_->mutable_socket_buffered_trace()->mutable_connection());
    sink_handle_->submitTrace(std::move(buffered_trace_));
    buffered_trace_.reset();
    current_buffered_rx_tx_bytes_ = 0;

    // It is always false for buffered trace.
    pegReadWriteSubmitCounter(false, isRead);
  }
}

void PerSocketTapperImpl::onRead(const Buffer::Instance& data, uint32_t bytes_read) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initStreamingEvent(event);
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(),
                                              config_->maxBufferedRxBytes(), data,
                                              data.length() - bytes_read, bytes_read);
    sink_handle_->submitTrace(std::move(trace));
    pegReadWriteSubmitCounter(true, true);
  } else {
    if (buffered_trace_ != nullptr && buffered_trace_->socket_buffered_trace().read_truncated()) {
      return;
    }

    makeBufferedTraceIfNeeded();
    auto& event = *buffered_trace_->mutable_socket_buffered_trace()->add_events();
    initEvent(event);
    if (should_sending_tapped_msg_on_configured_size_) {
      handleSendingTappedMsgPerConfigSize(data, bytes_read, event, true);
    } else {
      ASSERT(rx_bytes_buffered_ <= config_->maxBufferedRxBytes());
      buffered_trace_->mutable_socket_buffered_trace()->set_read_truncated(
          TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(),
                                                    config_->maxBufferedRxBytes() -
                                                        rx_bytes_buffered_,
                                                    data, data.length() - bytes_read, bytes_read));
      rx_bytes_buffered_ += event.read().data().as_bytes().size();
    }
  }
}

void PerSocketTapperImpl::onWrite(const Buffer::Instance& data, uint32_t bytes_written,
                                  bool end_stream) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initStreamingEvent(event);
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_write()->mutable_data(),
                                              config_->maxBufferedTxBytes(), data, 0,
                                              bytes_written);
    event.mutable_write()->set_end_stream(end_stream);
    sink_handle_->submitTrace(std::move(trace));
    pegReadWriteSubmitCounter(true, false);
  } else {
    if (buffered_trace_ != nullptr && buffered_trace_->socket_buffered_trace().write_truncated()) {
      return;
    }

    makeBufferedTraceIfNeeded();
    auto& event = *buffered_trace_->mutable_socket_buffered_trace()->add_events();
    initEvent(event);
    event.mutable_write()->set_end_stream(end_stream);
    if (should_sending_tapped_msg_on_configured_size_) {
      handleSendingTappedMsgPerConfigSize(data, bytes_written, event, false);
    } else {
      ASSERT(tx_bytes_buffered_ <= config_->maxBufferedTxBytes());
      buffered_trace_->mutable_socket_buffered_trace()->set_write_truncated(
          TapCommon::Utility::addBufferToProtoBytes(
              *event.mutable_write()->mutable_data(),
              config_->maxBufferedTxBytes() - tx_bytes_buffered_, data, 0, bytes_written));
      tx_bytes_buffered_ += event.write().data().as_bytes().size();
    }
  }
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

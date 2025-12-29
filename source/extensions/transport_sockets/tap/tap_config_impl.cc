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
    pegSubmitCounter(true);
  }
  seq_num++;
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

void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    seq_num++;
    if (shouldSendStreamedMsgByConfiguredSize()) {
      makeStreamedTraceIfNeeded();
      auto& event =
          *streamed_trace_->mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
      initStreamingEvent(event, seq_num);
      event.mutable_closed();
      // submit directly and don't check current_streamed_rx_tx_bytes_ any more
      submitStreamedDataPerConfiguredSize();
    } else {
      TapCommon::TraceWrapperPtr trace = makeTraceSegment();
      auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
      initStreamingEvent(event, seq_num);
      event.mutable_closed();
      sink_handle_->submitTrace(std::move(trace));
    }
    pegSubmitCounter(true);
  } else {
    makeBufferedTraceIfNeeded();
    fillConnectionInfo(*buffered_trace_->mutable_socket_buffered_trace()->mutable_connection());
    sink_handle_->submitTrace(std::move(buffered_trace_));
    pegSubmitCounter(false);
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

void PerSocketTapperImpl::initStreamingEvent(envoy::data::tap::v3::SocketEvent& event,
                                             uint64_t seq_num) {
  initEvent(event);
  if (should_output_conn_info_per_event_) {
    fillConnectionInfo(*event.mutable_connection());
  }
  event.set_seq_num(seq_num);
}

void PerSocketTapperImpl::pegSubmitCounter(const bool is_streaming) {
  if (is_streaming) {
    stats_.streamed_submit_.inc();
  } else {
    stats_.buffered_submit_.inc();
  }
}

bool PerSocketTapperImpl::shouldSendStreamedMsgByConfiguredSize() const {
  return config_->minStreamedSentBytes() > 0;
}

void PerSocketTapperImpl::submitStreamedDataPerConfiguredSize() {
  sink_handle_->submitTrace(std::move(streamed_trace_));
  streamed_trace_.reset();
  current_streamed_rx_tx_bytes_ = 0;
}

bool PerSocketTapperImpl::shouldSubmitStreamedDataPerConfiguredSizeByAgedDuration() const {
  if (streamed_trace_ == nullptr) {
    return false;
  }
  const envoy::data::tap::v3::SocketEvents& streamed_events =
      streamed_trace_->socket_streamed_trace_segment().events();
  auto& repeated_streamed_events = streamed_events.events();
  if (repeated_streamed_events.size() < 2) {
    // Only one event.
    return false;
  }

  const Protobuf::Timestamp& first_event_ts = repeated_streamed_events[0].timestamp();
  const Protobuf::Timestamp& last_event_ts =
      repeated_streamed_events[repeated_streamed_events.size() - 1].timestamp();
  return (last_event_ts.seconds() - first_event_ts.seconds()) >=
         static_cast<int64_t>(DefaultBufferedAgedDuration);
}

void PerSocketTapperImpl::handleSendingStreamTappedMsgPerConfigSize(const Buffer::Instance& data,
                                                                    const uint32_t total_bytes,
                                                                    const bool is_read,
                                                                    const bool is_end_stream) {
  makeStreamedTraceIfNeeded();
  auto& event =
      *streamed_trace_->mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
  initStreamingEvent(event, seq_num);
  uint32_t buffer_start_offset = 0;
  if (is_read) {
    buffer_start_offset = data.length() - total_bytes;
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(), total_bytes,
                                              data, buffer_start_offset, total_bytes);
    current_streamed_rx_tx_bytes_ += event.read().data().as_bytes().size();
  } else {
    event.mutable_write()->set_end_stream(is_end_stream);
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_write()->mutable_data(), total_bytes,
                                              data, buffer_start_offset, total_bytes);
    current_streamed_rx_tx_bytes_ += event.write().data().as_bytes().size();
  }

  if (current_streamed_rx_tx_bytes_ >= config_->minStreamedSentBytes() ||
      shouldSubmitStreamedDataPerConfiguredSizeByAgedDuration()) {
    submitStreamedDataPerConfiguredSize();
    pegSubmitCounter(true);
  }
}

void PerSocketTapperImpl::onRead(const Buffer::Instance& data, uint32_t bytes_read) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }
  if (config_->streaming()) {
    if (shouldSendStreamedMsgByConfiguredSize()) {
      handleSendingStreamTappedMsgPerConfigSize(data, bytes_read, true, false);
    } else {
      TapCommon::TraceWrapperPtr trace = makeTraceSegment();
      auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
      initStreamingEvent(event, seq_num);
      TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(),
                                                config_->maxBufferedRxBytes(), data,
                                                data.length() - bytes_read, bytes_read);
      sink_handle_->submitTrace(std::move(trace));
      pegSubmitCounter(true);
    }
    seq_num = seq_num + bytes_read;
  } else {
    if (buffered_trace_ != nullptr && buffered_trace_->socket_buffered_trace().read_truncated()) {
      return;
    }

    makeBufferedTraceIfNeeded();
    auto& event = *buffered_trace_->mutable_socket_buffered_trace()->add_events();
    initEvent(event);
    ASSERT(rx_bytes_buffered_ <= config_->maxBufferedRxBytes());
    buffered_trace_->mutable_socket_buffered_trace()->set_read_truncated(
        TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(),
                                                  config_->maxBufferedRxBytes() -
                                                      rx_bytes_buffered_,
                                                  data, data.length() - bytes_read, bytes_read));
    rx_bytes_buffered_ += event.read().data().as_bytes().size();
  }
}

void PerSocketTapperImpl::onWrite(const Buffer::Instance& data, uint32_t bytes_written,
                                  bool end_stream) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    if (shouldSendStreamedMsgByConfiguredSize()) {
      handleSendingStreamTappedMsgPerConfigSize(data, bytes_written, false, end_stream);
    } else {
      TapCommon::TraceWrapperPtr trace = makeTraceSegment();
      auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
      initStreamingEvent(event, seq_num);
      TapCommon::Utility::addBufferToProtoBytes(*event.mutable_write()->mutable_data(),
                                                config_->maxBufferedTxBytes(), data, 0,
                                                bytes_written);
      event.mutable_write()->set_end_stream(end_stream);
      sink_handle_->submitTrace(std::move(trace));
      pegSubmitCounter(true);
    }
    seq_num = seq_num + bytes_written;
  } else {
    if (buffered_trace_ != nullptr && buffered_trace_->socket_buffered_trace().write_truncated()) {
      return;
    }

    makeBufferedTraceIfNeeded();
    auto& event = *buffered_trace_->mutable_socket_buffered_trace()->add_events();
    initEvent(event);
    ASSERT(tx_bytes_buffered_ <= config_->maxBufferedTxBytes());
    buffered_trace_->mutable_socket_buffered_trace()->set_write_truncated(
        TapCommon::Utility::addBufferToProtoBytes(
            *event.mutable_write()->mutable_data(),
            config_->maxBufferedTxBytes() - tx_bytes_buffered_, data, 0, bytes_written));
    tx_bytes_buffered_ += event.write().data().as_bytes().size();
    event.mutable_write()->set_end_stream(end_stream);
  }
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#include "extensions/transport_sockets/tap/tap_config_impl.h"

#include "envoy/data/tap/v3/transport.pb.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

namespace TapCommon = Extensions::Common::Tap;

PerSocketTapperImpl::PerSocketTapperImpl(SocketTapConfigSharedPtr config,
                                         const Network::Connection& connection)
    : config_(std::move(config)),
      sink_handle_(config_->createPerTapSinkHandleManager(connection.id())),
      connection_(connection), statuses_(config_->createMatchStatusVector()) {
  config_->rootMatcher().onNewStream(statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    fillConnectionInfo(*trace->mutable_socket_streamed_trace_segment()->mutable_connection());
    sink_handle_->submitTrace(std::move(trace));
  }
}

void PerSocketTapperImpl::fillConnectionInfo(envoy::data::tap::v3::Connection& connection) {
  Network::Utility::addressToProtobufAddress(*connection_.localAddress(),
                                             *connection.mutable_local_address());
  Network::Utility::addressToProtobufAddress(*connection_.remoteAddress(),
                                             *connection.mutable_remote_address());
}

void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initEvent(event);
    event.mutable_closed();
    sink_handle_->submitTrace(std::move(trace));
  } else {
    makeBufferedTraceIfNeeded();
    fillConnectionInfo(*buffered_trace_->mutable_socket_buffered_trace()->mutable_connection());
    sink_handle_->submitTrace(std::move(buffered_trace_));
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

void PerSocketTapperImpl::onRead(const Buffer::Instance& data, uint32_t bytes_read) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (config_->streaming()) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initEvent(event);
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_read()->mutable_data(),
                                              config_->maxBufferedRxBytes(), data,
                                              data.length() - bytes_read, bytes_read);
    sink_handle_->submitTrace(std::move(trace));
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
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    auto& event = *trace->mutable_socket_streamed_trace_segment()->mutable_event();
    initEvent(event);
    TapCommon::Utility::addBufferToProtoBytes(*event.mutable_write()->mutable_data(),
                                              config_->maxBufferedTxBytes(), data, 0,
                                              bytes_written);
    event.mutable_write()->set_end_stream(end_stream);
    sink_handle_->submitTrace(std::move(trace));
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

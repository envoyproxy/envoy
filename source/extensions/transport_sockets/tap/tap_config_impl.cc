#include "extensions/transport_sockets/tap/tap_config_impl.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

namespace TapCommon = Extensions::Common::Tap;

PerSocketTapperImpl::PerSocketTapperImpl(SocketTapConfigImplSharedPtr config,
                                         const Network::Connection& connection)
    : config_(std::move(config)),
      sink_handle_(config_->createPerTapSinkHandleManager(connection.id())),
      connection_(connection), statuses_(config_->createMatchStatusVector()),
      trace_(TapCommon::makeTraceWrapper()) {
  config_->rootMatcher().onNewStream(statuses_);
}

void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  auto* connection = trace_->mutable_socket_buffered_trace()->mutable_connection();
  connection->set_id(connection_.id());
  Network::Utility::addressToProtobufAddress(*connection_.localAddress(),
                                             *connection->mutable_local_address());
  Network::Utility::addressToProtobufAddress(*connection_.remoteAddress(),
                                             *connection->mutable_remote_address());
  sink_handle_->submitTrace(trace_);
}

envoy::data::tap::v2alpha::SocketEvent& PerSocketTapperImpl::createEvent() {
  auto* event = trace_->mutable_socket_buffered_trace()->add_events();
  event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->time_source_.systemTime().time_since_epoch())
          .count()));
  return *event;
}

void PerSocketTapperImpl::onRead(const Buffer::Instance& data, uint32_t bytes_read) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (trace_->socket_buffered_trace().read_truncated()) {
    return;
  }

  auto& event = createEvent();
  ASSERT(rx_bytes_buffered_ <= config_->maxBufferedRxBytes());
  trace_->mutable_socket_buffered_trace()->set_read_truncated(
      Extensions::Common::Tap::Utility::addBufferToProtoBytes(
          *event.mutable_read()->mutable_data(), config_->maxBufferedRxBytes() - rx_bytes_buffered_,
          data, data.length() - bytes_read, bytes_read));
  rx_bytes_buffered_ += event.read().data().as_bytes().size();
}

void PerSocketTapperImpl::onWrite(const Buffer::Instance& data, uint32_t bytes_written,
                                  bool end_stream) {
  if (!config_->rootMatcher().matchStatus(statuses_).matches_) {
    return;
  }

  if (trace_->socket_buffered_trace().write_truncated()) {
    return;
  }

  auto& event = createEvent();
  ASSERT(tx_bytes_buffered_ <= config_->maxBufferedTxBytes());
  trace_->mutable_socket_buffered_trace()->set_write_truncated(
      Extensions::Common::Tap::Utility::addBufferToProtoBytes(
          *event.mutable_write()->mutable_data(),
          config_->maxBufferedTxBytes() - tx_bytes_buffered_, data, 0, bytes_written));
  tx_bytes_buffered_ += event.write().data().as_bytes().size();

  event.mutable_write()->set_end_stream(end_stream);
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

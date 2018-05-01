#include "extensions/transport_sockets/capture/capture.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Capture {

CaptureSocket::CaptureSocket(
    const std::string& path_prefix,
    envoy::config::transport_socket::capture::v2alpha::FileSink::Format format,
    Network::TransportSocketPtr&& transport_socket)
    : path_prefix_(path_prefix), format_(format), transport_socket_(std::move(transport_socket)) {}

void CaptureSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  transport_socket_->setTransportSocketCallbacks(callbacks);
}

std::string CaptureSocket::protocol() const { return transport_socket_->protocol(); }

bool CaptureSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

void CaptureSocket::closeSocket(Network::ConnectionEvent event) {
  // The caller should have invoked setTransportSocketCallbacks() prior to this.
  ASSERT(callbacks_ != nullptr);
  auto* connection = trace_.mutable_connection();
  connection->set_id(callbacks_->connection().id());
  Network::Utility::addressToProtobufAddress(*callbacks_->connection().localAddress(),
                                             *connection->mutable_local_address());
  Network::Utility::addressToProtobufAddress(*callbacks_->connection().remoteAddress(),
                                             *connection->mutable_remote_address());
  const bool text_format =
      format_ == envoy::config::transport_socket::capture::v2alpha::FileSink::PROTO_TEXT;
  const std::string path = fmt::format("{}_{}.{}", path_prefix_, callbacks_->connection().id(),
                                       text_format ? "pb_text" : "pb");
  ENVOY_LOG_MISC(debug, "Writing socket trace for [C{}] to {}", callbacks_->connection().id(),
                 path);
  ENVOY_LOG_MISC(trace, "Socket trace for [C{}]: {}", callbacks_->connection().id(),
                 trace_.DebugString());
  std::ofstream proto_stream(path);
  if (text_format) {
    proto_stream << trace_.DebugString();
  } else {
    trace_.SerializeToOstream(&proto_stream);
  }
  transport_socket_->closeSocket(event);
}

Network::IoResult CaptureSocket::doRead(Buffer::Instance& buffer) {
  Network::IoResult result = transport_socket_->doRead(buffer);
  if (result.bytes_processed_ > 0) {
    // TODO(htuch): avoid linearizing
    char* data = static_cast<char*>(buffer.linearize(buffer.length())) +
                 (buffer.length() - result.bytes_processed_);
    auto* event = trace_.add_events();
    event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count()));
    event->mutable_read()->set_data(data, result.bytes_processed_);
  }

  return result;
}

Network::IoResult CaptureSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  // TODO(htuch): avoid copy.
  Buffer::OwnedImpl copy(buffer);
  Network::IoResult result = transport_socket_->doWrite(buffer, end_stream);
  if (result.bytes_processed_ > 0) {
    // TODO(htuch): avoid linearizing.
    char* data = static_cast<char*>(copy.linearize(result.bytes_processed_));
    auto* event = trace_.add_events();
    event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count()));
    event->mutable_write()->set_data(data, result.bytes_processed_);
    event->mutable_write()->set_end_stream(end_stream);
  }
  return result;
}

void CaptureSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::Connection* CaptureSocket::ssl() { return transport_socket_->ssl(); }

const Ssl::Connection* CaptureSocket::ssl() const { return transport_socket_->ssl(); }

CaptureSocketFactory::CaptureSocketFactory(
    const std::string& path_prefix,
    envoy::config::transport_socket::capture::v2alpha::FileSink::Format format,
    Network::TransportSocketFactoryPtr&& transport_socket_factory)
    : path_prefix_(path_prefix), format_(format),
      transport_socket_factory_(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr CaptureSocketFactory::createTransportSocket() const {
  return std::make_unique<CaptureSocket>(path_prefix_, format_,
                                         transport_socket_factory_->createTransportSocket());
}

bool CaptureSocketFactory::implementsSecureTransport() const {
  return transport_socket_factory_->implementsSecureTransport();
}

} // namespace Capture
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

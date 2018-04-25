#include "extensions/transport_sockets/capture/capture.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Capture {

CaptureSocket::CaptureSocket(const std::string& path, bool text_format,
                             Network::TransportSocketPtr&& transport_socket)
    : path_(path), text_format_(text_format), transport_socket_(std::move(transport_socket)) {}

void CaptureSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
}

std::string CaptureSocket::protocol() const { return transport_socket_->protocol(); }

bool CaptureSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

void CaptureSocket::closeSocket(Network::ConnectionEvent event) {
  std::ofstream proto_stream(path_);
  if (text_format_) {
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
    trace_.add_events()->mutable_read()->set_data(data, result.bytes_processed_);
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
    trace_.add_events()->mutable_write()->set_data(data, result.bytes_processed_);
  }
  return result;
}

void CaptureSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::Connection* CaptureSocket::ssl() { return transport_socket_->ssl(); }

const Ssl::Connection* CaptureSocket::ssl() const { return transport_socket_->ssl(); }

CaptureSocketFactory::CaptureSocketFactory(
    const std::string& path_prefix, bool text_format,
    Network::TransportSocketFactoryPtr&& transport_socket_factory)
    : path_prefix_(path_prefix), text_format_(text_format),
      transport_socket_factory_(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr CaptureSocketFactory::createTransportSocket() {
  return std::make_unique<CaptureSocket>(
      fmt::format("{}_{}.{}", path_prefix_, socket_id_++, text_format_ ? "pb_text" : "pb"),
      text_format_, transport_socket_factory_->createTransportSocket());
}

bool CaptureSocketFactory::implementsSecureTransport() const {
  return transport_socket_factory_->implementsSecureTransport();
}

} // namespace Capture
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

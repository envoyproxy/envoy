#include "extensions/transport_sockets/starttls/starttls_socket.h"


#include <iostream>

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

using absl::ascii_isdigit;

Network::IoResult StartTlsSocket::doRead(Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "starttls: doRead ({}) {}", buffer.length(), buffer.toString());

  buffer.drain(buffer.length());
  if (passthrough_) {
    return passthrough_->doRead(buffer);
  } else {
    return raw_socket_->doRead(buffer);
  }
}

Network::IoResult StartTlsSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG(trace, "starttls: doWrite ({}) {}", buffer.length(), buffer.toString());

  if (passthrough_) {
    return passthrough_->doWrite(buffer, end_stream);
  }

  Network::IoResult result = raw_socket_->doWrite(buffer, end_stream);
  result.bytes_processed_ = buffer.length();

/*
  if (switch_to_ssl_) {
    if (!passthrough_) {
      ssl_socket_->setTransportSocketCallbacks(*callbacks_);
      ssl_socket_->onConnected();
      passthrough_ = std::move(ssl_socket_);
      raw_socket_.reset();
    }
  }
*/

  return result;
}

/*
 * Indicate that transport socket should switch to SSL.
 * This will happen after the next write.
 * The switch cannot be done in-place here, because it may be called from upstream filter and in
 * that case we are in the middle of a transaction.
 * */
bool StartTlsSocket::startSecureTransport() {
  switch_to_ssl_ = true;
    if (!passthrough_) {
      ssl_socket_->setTransportSocketCallbacks(*callbacks_);
      ssl_socket_->onConnected();
      passthrough_ = std::move(ssl_socket_);
      raw_socket_.reset();
    }
  return true;
}

// TODO: right now this just expects DownstreamTlsContext in
// TransportSocket.typed_config which it passes to both transport sockets. There
// probably needs to be a separate config proto for this that can hold the
// config protos for both RawBuffer/SslSocket.
Network::TransportSocketPtr ServerStartTlsSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  ENVOY_LOG(trace, "starttls: createTransportSocket");
  return std::make_unique<StartTlsSocket>(config_,
      raw_socket_factory_->createTransportSocket(transport_socket_options),
      tls_socket_factory_->createTransportSocket(transport_socket_options),
      transport_socket_options);
}

ServerStartTlsSocketFactory::~ServerStartTlsSocketFactory() {}

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

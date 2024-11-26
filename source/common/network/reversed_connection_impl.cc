#include "source/common/network/reversed_connection_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {

ReversedClientConnectionImpl::ReversedClientConnectionImpl(
    Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address, Event::Dispatcher& dispatcher,
    Network::TransportSocketPtr&& transport_socket,
    Network::ConnectionSocketPtr&& downstream_socket, const bool expects_proxy_protocol)
    : ClientConnectionImpl(dispatcher, std::move(transport_socket), std::move(downstream_socket)),
      remote_address_(address), source_address_(source_address),
      expects_proxy_protocol_(expects_proxy_protocol) {}

void ReversedClientConnectionImpl::connect() {
  ENVOY_LOG(debug, "ReversedClientConnectionImpl::connect");
  if (expects_proxy_protocol_) {
    // This is a connection to an older remote site that expects a proxy protocol header.
    SendProxyProtocolHeader();
  }
  ENVOY_CONN_LOG(debug, "Already connected to {}", *this,
                 socket_->connectionInfoProvider().remoteAddress()->asString());
}

void ReversedClientConnectionImpl::SendProxyProtocolHeader() {
  ENVOY_CONN_LOG(debug, "Sending a proxy protocol header to {}", *this,
                 socket_->connectionInfoProvider().remoteAddress()->asString());
  std::string proxy_protocol_header = "PROXY TCP4 127.0.0.1 127.0.0.1 65535 ";
  absl::StrAppend(&proxy_protocol_header, remote_address_->ip()->port(), "\r\n");
  ssize_t nwritten = ::write(ioHandle().fdDoNotUse(), proxy_protocol_header.c_str(),
                             proxy_protocol_header.length());
  if (nwritten < 1) {
    immediate_error_event_ = ConnectionEvent::RemoteClose;
    connecting_ = false;
    ENVOY_CONN_LOG(debug, "immediate connection error: {}", *this, errno);

    // Trigger a write event. This is needed on OSX and seems harmless on Linux.
    ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
}

void ReversedClientConnectionImpl::close(ConnectionCloseType type, absl::string_view details) {
  dispatcher_.connectionHandler()->reverseConnRegistry().getRCHandler().markSocketDead(
      socket_->ioHandle().fdDoNotUse(), true /* used */);
  ClientConnectionImpl::close(type, details);
}

} // namespace Network
} // namespace Envoy

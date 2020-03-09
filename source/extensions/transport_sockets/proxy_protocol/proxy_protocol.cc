#include <sstream>

#include "envoy/network/transport_socket.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/proxy_protocol.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ONBEHALF_OF;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE_LEN;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_TRANSPORT_STREAM;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_VERSION;
using envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocol_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

ProxyProtocolSocket::ProxyProtocolSocket(Network::TransportSocketPtr transport_socket,
                                         Network::TransportSocketOptionsSharedPtr options,
                                         ProxyProtocol_Version version)
    : transport_socket_(std::move(transport_socket)), options_(options), version_(version) {}

void ProxyProtocolSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
}

std::string ProxyProtocolSocket::protocol() const { return transport_socket_->protocol(); }

absl::string_view ProxyProtocolSocket::failureReason() const {
  return transport_socket_->failureReason();
}

bool ProxyProtocolSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

void ProxyProtocolSocket::closeSocket(Network::ConnectionEvent event) {
  transport_socket_->closeSocket(event);
}

Network::IoResult ProxyProtocolSocket::doRead(Buffer::Instance& buffer) {
  return transport_socket_->doRead(buffer);
}

Network::IoResult ProxyProtocolSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (!injected_header_) {
    auto headerRes = injectHeader();
    if (headerRes.action_ == Network::PostIoAction::Close) {
      return headerRes;
    }
    injected_header_ = true;
    auto res = transport_socket_->doWrite(buffer, end_stream);
    res.bytes_processed_ += headerRes.bytes_processed_;
    return res;
  }
  return transport_socket_->doWrite(buffer, end_stream);
}

Network::IoResult ProxyProtocolSocket::injectHeader() {
  if (version_ == ProxyProtocol_Version::ProxyProtocol_Version_V1) {
    return injectHeaderV1();
  } else {
    return injectHeaderV2();
  }
}

Network::IoResult ProxyProtocolSocket::injectHeaderV1() {
  std::ostringstream stream;
  stream << PROXY_PROTO_V1_SIGNATURE;
  // Default to local addresses
  std::string src_address = callbacks_->connection().localAddress()->ip()->addressAsString();
  std::string dst_address = callbacks_->connection().remoteAddress()->ip()->addressAsString();
  auto src_port = callbacks_->connection().localAddress()->ip()->port();
  auto dst_port = callbacks_->connection().remoteAddress()->ip()->port();
  auto ip_version = callbacks_->connection().localAddress()->ip()->version();

  if (options_ && options_->downstreamAddresses().has_value()) {
    const auto down_addrs = options_->downstreamAddresses().value();
    ip_version = down_addrs.version_;
    src_address = down_addrs.remote_addr_;
    dst_address = down_addrs.local_addr_;
    src_port = down_addrs.remote_port_;
    dst_port = down_addrs.local_port_;
  }

  Buffer::OwnedImpl buff{};
  Common::ProxyProtocol::generateV1Header(src_address, dst_address, src_port, dst_port, ip_version, buff);
  auto res = buff.write(callbacks_->ioHandle());
  if (!res.ok()) {
    ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(),
                  res.err_->getErrorDetails());
    return Network::IoResult{Network::PostIoAction::Close, res.rc_, false};
  }
  return Network::IoResult{Network::PostIoAction::KeepOpen, res.rc_, false};
}

Network::IoResult ProxyProtocolSocket::injectHeaderV2() {
  Buffer::OwnedImpl buff{};

  if (!options_ || !options_->downstreamAddresses().has_value()) {
    Common::ProxyProtocol::generateV2LocalHeader(buff);
  } else {
    const auto down_addrs = options_->downstreamAddresses().value();
    Common::ProxyProtocol::generateV2Header(down_addrs.remote_addr_, down_addrs.local_addr_, down_addrs.remote_port_, down_addrs.local_port_, down_addrs.version_, buff);
  }

  auto res = buff.write(callbacks_->ioHandle());
  if (!res.ok()) {
    ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(),
                  res.err_->getErrorDetails());
    return Network::IoResult{Network::PostIoAction::Close, res.rc_, false};
  }
  return Network::IoResult{Network::PostIoAction::KeepOpen, res.rc_, false};
}

void ProxyProtocolSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::ConnectionInfoConstSharedPtr ProxyProtocolSocket::ssl() const {
  return transport_socket_->ssl();
}

ProxyProtocolSocketFactory::ProxyProtocolSocketFactory(
    Network::TransportSocketFactoryPtr transport_socket_factory, ProxyProtocol_Version version)
    : transport_socket_factory_(std::move(transport_socket_factory)), version_(version) {}

Network::TransportSocketPtr ProxyProtocolSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr options) const {
  return std::make_unique<ProxyProtocolSocket>(
      transport_socket_factory_->createTransportSocket(options), options, version_);
}

bool ProxyProtocolSocketFactory::implementsSecureTransport() const {
  return transport_socket_factory_->implementsSecureTransport();
}

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/network/socket_option_factory.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address) {
  return quic_address.IsInitialized()
             ? Network::Address::addressFromSockAddr(quic_address.generic_address(),
                                                     quic_address.host().address_family() ==
                                                             quic::IpAddressFamily::IP_V4
                                                         ? sizeof(sockaddr_in)
                                                         : sizeof(sockaddr_in6),
                                                     false)
             : nullptr;
}

quic::QuicSocketAddress envoyAddressInstanceToQuicSocketAddress(
    const Network::Address::InstanceConstSharedPtr& envoy_address) {
  ASSERT(envoy_address != nullptr && envoy_address->type() == Network::Address::Type::Ip);
  uint32_t port = envoy_address->ip()->port();
  sockaddr_storage ss;
  if (envoy_address->ip()->version() == Network::Address::IpVersion::v4) {
    auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
    memset(ipv4_addr, 0, sizeof(sockaddr_in));
    ipv4_addr->sin_family = AF_INET;
    ipv4_addr->sin_port = htons(port);
    ipv4_addr->sin_addr.s_addr = envoy_address->ip()->ipv4()->address();
  } else {
    auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
    memset(ipv6_addr, 0, sizeof(sockaddr_in6));
    ipv6_addr->sin6_family = AF_INET6;
    ipv6_addr->sin6_port = htons(port);
    ASSERT(sizeof(ipv6_addr->sin6_addr.s6_addr) == 16u);
    *reinterpret_cast<absl::uint128*>(ipv6_addr->sin6_addr.s6_addr) =
        envoy_address->ip()->ipv6()->address();
  }
  return quic::QuicSocketAddress(ss);
}

spdy::SpdyHeaderBlock envoyHeadersToSpdyHeaderBlock(const Http::HeaderMap& headers) {
  spdy::SpdyHeaderBlock header_block;
  headers.iterate([&header_block](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    // The key-value pairs are copied.
    header_block.insert({header.key().getStringView(), header.value().getStringView()});
    return Http::HeaderMap::Iterate::Continue;
  });
  return header_block;
}

quic::QuicRstStreamErrorCode envoyResetReasonToQuicRstError(Http::StreamResetReason reason) {
  switch (reason) {
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return quic::QUIC_REFUSED_STREAM;
  case Http::StreamResetReason::ConnectionFailure:
    return quic::QUIC_STREAM_CONNECTION_ERROR;
  case Http::StreamResetReason::LocalReset:
    return quic::QUIC_STREAM_CANCELLED;
  case Http::StreamResetReason::ConnectionTermination:
    return quic::QUIC_STREAM_NO_ERROR;
  default:
    return quic::QUIC_BAD_APPLICATION_PAYLOAD;
  }
}

Http::StreamResetReason quicRstErrorToEnvoyResetReason(quic::QuicRstStreamErrorCode rst_err) {
  switch (rst_err) {
  case quic::QUIC_REFUSED_STREAM:
    return Http::StreamResetReason::RemoteRefusedStreamReset;
  default:
    return Http::StreamResetReason::RemoteReset;
  }
}

Http::StreamResetReason quicErrorCodeToEnvoyResetReason(quic::QuicErrorCode error) {
  if (error == quic::QUIC_NO_ERROR) {
    return Http::StreamResetReason::ConnectionTermination;
  } else {
    return Http::StreamResetReason::ConnectionFailure;
  }
}

Http::GoAwayErrorCode quicErrorCodeToEnvoyErrorCode(quic::QuicErrorCode error) noexcept {
  switch (error) {
  case quic::QUIC_NO_ERROR:
    return Http::GoAwayErrorCode::NoError;
  default:
    return Http::GoAwayErrorCode::Other;
  }
}

Network::ConnectionSocketPtr
createConnectionSocket(Network::Address::InstanceConstSharedPtr& peer_addr,
                       Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::ConnectionSocket::OptionsSharedPtr& options) {
  auto connection_socket = std::make_unique<Network::ConnectionSocketImpl>(
      Network::Socket::Type::Datagram, local_addr, peer_addr);
  connection_socket->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
  connection_socket->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
  if (options != nullptr) {
    connection_socket->addOptions(options);
  }
  if (!Network::Socket::applyOptions(connection_socket->options(), *connection_socket,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    connection_socket->close();
    ENVOY_LOG_MISC(error, "Fail to apply pre-bind options");
    return connection_socket;
  }
  connection_socket->bind(local_addr);
  ASSERT(local_addr->ip());
  local_addr = connection_socket->localAddress();
  if (!Network::Socket::applyOptions(connection_socket->options(), *connection_socket,
                                     envoy::config::core::v3::SocketOption::STATE_BOUND)) {
    ENVOY_LOG_MISC(error, "Fail to apply post-bind options");
    connection_socket->close();
  }
  return connection_socket;
}

} // namespace Quic
} // namespace Envoy

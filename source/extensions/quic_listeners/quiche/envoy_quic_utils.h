#include <sys/socket.h>

#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
inline Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address) {
  ASSERT(quic_address.host().address_family() != quic::IpAddressFamily::IP_UNSPEC);
  return quic_address.IsInitialized()
             ? Network::Address::addressFromSockAddr(quic_address.generic_address(),
                                                     quic_address.host().address_family() ==
                                                             quic::IpAddressFamily::IP_V4
                                                         ? sizeof(sockaddr_in)
                                                         : sizeof(sockaddr_in6),
                                                     false)
             : nullptr;
}

inline quic::QuicSocketAddress envoyAddressInstanceToQuicSocketAddress(
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
    *reinterpret_cast<absl::uint128*>(ipv6_addr->sin6_addr.s6_addr) =
        envoy_address->ip()->ipv6()->address();
  }
  return quic::QuicSocketAddress(ss);
}

inline Http::HeaderMapImplPtr quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list) {
  Http::HeaderMapImplPtr headers = std::make_unique<Http::HeaderMapImpl>();
  for (const auto& entry : header_list) {
    // TODO(danzh): Avoid copy by referencing entry as header_list is already validated by QUIC.
    headers->addCopy(Http::LowerCaseString(entry.first), entry.second);
  }
  return headers;
}

inline Http::HeaderMapImplPtr
spdyHeaderBlockToEnvoyHeaders(const spdy::SpdyHeaderBlock& header_block) {
  Http::HeaderMapImplPtr headers = std::make_unique<Http::HeaderMapImpl>();
  for (auto entry : header_block) {
    // TODO(danzh): Avoid temporary strings and addCopy() with std::string_view.
    std::string key(entry.first);
    std::string value(entry.second);
    headers->addCopy(Http::LowerCaseString(key), value);
  }
  return headers;
}

spdy::SpdyHeaderBlock envoyHeadersToSpdyHeaderBlock(const Http::HeaderMap& headers) {
  spdy::SpdyHeaderBlock header_block;
  headers.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        auto spdy_headers = static_cast<spdy::SpdyHeaderBlock*>(context);
        // The key-value pairs are copied.
        spdy_headers->insert({header.key().getStringView(), header.value().getStringView()});
        return Http::HeaderMap::Iterate::Continue;
      },
      &header_block);
  return header_block;
}

inline Http::StreamResetReason
quicRstErrorToEnvoyResetReason(quic::QuicRstStreamErrorCode quic_rst) {
  switch (quic_rst) {
  case quic::QUIC_REFUSED_STREAM:
    return Http::StreamResetReason::RemoteRefusedStreamReset;
  case quic::QUIC_STREAM_NO_ERROR:
    return Http::StreamResetReason::ConnectionTermination;
  case quic::QUIC_STREAM_CONNECTION_ERROR:
    return Http::StreamResetReason::ConnectionFailure;
  default:
    return Http::StreamResetReason::RemoteReset;
  }
}

} // namespace Quic
} // namespace Envoy

#pragma once

#include "envoy/common/platform.h"
#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_types.h"

#pragma GCC diagnostic pop

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address);

quic::QuicSocketAddress envoyAddressInstanceToQuicSocketAddress(
    const Network::Address::InstanceConstSharedPtr& envoy_address);

// The returned header map has all keys in lower case.
template <class T>
std::unique_ptr<T> quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list) {
  auto headers = std::make_unique<T>();
  for (const auto& entry : header_list) {
    // TODO(danzh): Avoid copy by referencing entry as header_list is already validated by QUIC.
    headers->addCopy(Http::LowerCaseString(entry.first), entry.second);
  }
  return headers;
}

template <class T>
std::unique_ptr<T> spdyHeaderBlockToEnvoyHeaders(const spdy::SpdyHeaderBlock& header_block) {
  auto headers = std::make_unique<T>();
  for (auto entry : header_block) {
    // TODO(danzh): Avoid temporary strings and addCopy() with std::string_view.
    std::string key(entry.first);
    std::string value(entry.second);
    headers->addCopy(Http::LowerCaseString(key), value);
  }
  return headers;
}

spdy::SpdyHeaderBlock envoyHeadersToSpdyHeaderBlock(const Http::HeaderMap& headers);

// Called when Envoy wants to reset the underlying QUIC stream.
quic::QuicRstStreamErrorCode envoyResetReasonToQuicRstError(Http::StreamResetReason reason);

// Called when a RST_STREAM frame is received.
Http::StreamResetReason quicRstErrorToEnvoyResetReason(quic::QuicRstStreamErrorCode rst_err);

// Called when underlying QUIC connection is closed either locally or by peer.
Http::StreamResetReason quicErrorCodeToEnvoyResetReason(quic::QuicErrorCode error);

// Create a connection socket instance and apply given socket options to the
// socket. IP_PKTINFO and SO_RXQ_OVFL is always set if supported.
Network::ConnectionSocketPtr
createConnectionSocket(Network::Address::InstanceConstSharedPtr& peer_addr,
                       Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::ConnectionSocket::OptionsSharedPtr& options);

} // namespace Quic
} // namespace Envoy

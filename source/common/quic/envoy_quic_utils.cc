#include "source/common/quic/envoy_quic_utils.h"

#include <memory>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Quic {

namespace {

Network::Address::InstanceConstSharedPtr
getLoopbackAddress(Network::Address::InstanceConstSharedPtr peer_address) {
  if (peer_address->ip()->version() == Network::Address::IpVersion::v6) {
    return std::make_shared<Network::Address::Ipv6Instance>(
        "::1", 0, &peer_address->socketInterface(), peer_address->ip()->ipv6()->v6only());
  }
  return std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1",
                                                          &peer_address->socketInterface());
}

} // namespace

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address) {
  return quic_address.IsInitialized()
             ? Network::Address::addressFromSockAddrOrDie(quic_address.generic_address(),
                                                          quic_address.host().address_family() ==
                                                                  quiche::IpAddressFamily::IP_V4
                                                              ? sizeof(sockaddr_in)
                                                              : sizeof(sockaddr_in6),
                                                          -1, false)
             : nullptr;
}

quic::QuicSocketAddress envoyIpAddressToQuicSocketAddress(const Network::Address::Ip* envoy_ip) {
  if (envoy_ip == nullptr) {
    // Return uninitialized socket addr
    return {};
  }

  uint32_t port = envoy_ip->port();
  sockaddr_storage ss;

  if (envoy_ip->version() == Network::Address::IpVersion::v4) {
    // Create and return quic ipv4 address
    auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
    memset(ipv4_addr, 0, sizeof(sockaddr_in));
    ipv4_addr->sin_family = AF_INET;
    ipv4_addr->sin_port = htons(port);
    ipv4_addr->sin_addr.s_addr = envoy_ip->ipv4()->address();
  } else {
    // Create and return quic ipv6 address
    auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
    memset(ipv6_addr, 0, sizeof(sockaddr_in6));
    ipv6_addr->sin6_family = AF_INET6;
    ipv6_addr->sin6_port = htons(port);
    ASSERT(sizeof(ipv6_addr->sin6_addr.s6_addr) == 16u);
    *reinterpret_cast<absl::uint128*>(ipv6_addr->sin6_addr.s6_addr) = envoy_ip->ipv6()->address();
  }
  return quic::QuicSocketAddress(ss);
}

quiche::HttpHeaderBlock envoyHeadersToHttp2HeaderBlock(const Http::HeaderMap& headers) {
  quiche::HttpHeaderBlock header_block;
  headers.iterate([&header_block](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    // The key-value pairs are copied.
    header_block.AppendValueOrAddHeader(header.key().getStringView(),
                                        header.value().getStringView());
    return Http::HeaderMap::Iterate::Continue;
  });
  return header_block;
}

quic::QuicRstStreamErrorCode envoyResetReasonToQuicRstError(Http::StreamResetReason reason) {
  switch (reason) {
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return quic::QUIC_REFUSED_STREAM;
  case Http::StreamResetReason::LocalConnectionFailure:
  case Http::StreamResetReason::RemoteConnectionFailure:
  case Http::StreamResetReason::ConnectionTimeout:
  case Http::StreamResetReason::ConnectionTermination:
    return quic::QUIC_STREAM_CONNECTION_ERROR;
  case Http::StreamResetReason::ConnectError:
    return quic::QUIC_STREAM_CONNECT_ERROR;
  case Http::StreamResetReason::LocalReset:
    return quic::QUIC_STREAM_REQUEST_REJECTED;
  case Http::StreamResetReason::OverloadManager:
    return quic::QUIC_STREAM_CANCELLED;
  case Http::StreamResetReason::ProtocolError:
    return quic::QUIC_STREAM_GENERAL_PROTOCOL_ERROR;
  case Http::StreamResetReason::Overflow:
    IS_ENVOY_BUG("Resource overflow shouldn't be propergated to QUIC network stack");
    break;
  case Http::StreamResetReason::RemoteRefusedStreamReset:
  case Http::StreamResetReason::RemoteReset:
    IS_ENVOY_BUG("Remote reset shouldn't be initiated by self.");
    break;
  case Http::StreamResetReason::Http1PrematureUpstreamHalfClose:
    IS_ENVOY_BUG("H/1 premature response reset is not applicable to H/3.");
    break;
  }

  ENVOY_LOG_MISC(error, absl::StrCat("Unknown reset reason: ", reason));
  return quic::QUIC_STREAM_UNKNOWN_APPLICATION_ERROR_CODE;
}

Http::StreamResetReason quicRstErrorToEnvoyLocalResetReason(quic::QuicRstStreamErrorCode rst_err) {
  switch (rst_err) {
  case quic::QUIC_REFUSED_STREAM:
    return Http::StreamResetReason::LocalRefusedStreamReset;
  case quic::QUIC_STREAM_CONNECTION_ERROR:
    return Http::StreamResetReason::LocalConnectionFailure;
  case quic::QUIC_STREAM_NO_ERROR:
  case quic::QUIC_STREAM_EXCESSIVE_LOAD:
  case quic::QUIC_HEADERS_TOO_LARGE:
  case quic::QUIC_STREAM_REQUEST_REJECTED:
    return Http::StreamResetReason::LocalReset;
  case quic::QUIC_STREAM_CANCELLED:
    return Http::StreamResetReason::OverloadManager;
  default:
    return Http::StreamResetReason::ProtocolError;
  }
}

Http::StreamResetReason quicRstErrorToEnvoyRemoteResetReason(quic::QuicRstStreamErrorCode rst_err) {
  switch (rst_err) {
  case quic::QUIC_REFUSED_STREAM:
    return Http::StreamResetReason::RemoteRefusedStreamReset;
  case quic::QUIC_STREAM_CONNECTION_ERROR:
    return Http::StreamResetReason::ConnectionTermination;
  case quic::QUIC_STREAM_CONNECT_ERROR:
    return Http::StreamResetReason::ConnectError;
  case quic::QUIC_STREAM_NO_ERROR:
  case quic::QUIC_STREAM_REQUEST_REJECTED:
  case quic::QUIC_STREAM_UNKNOWN_APPLICATION_ERROR_CODE:
  case quic::QUIC_STREAM_EXCESSIVE_LOAD:
  case quic::QUIC_HEADERS_TOO_LARGE:
    return Http::StreamResetReason::RemoteReset;
  case quic::QUIC_STREAM_CANCELLED:
    return Http::StreamResetReason::OverloadManager;
  case quic::QUIC_STREAM_GENERAL_PROTOCOL_ERROR:
  default:
    return Http::StreamResetReason::ProtocolError;
  }
}

Http::StreamResetReason quicErrorCodeToEnvoyLocalResetReason(quic::QuicErrorCode error,
                                                             bool connected) {
  switch (error) {
  case quic::QUIC_HANDSHAKE_FAILED:
  case quic::QUIC_HANDSHAKE_TIMEOUT:
    return Http::StreamResetReason::LocalConnectionFailure;
  case quic::QUIC_PACKET_WRITE_ERROR:
  case quic::QUIC_NETWORK_IDLE_TIMEOUT:
    return connected ? Http::StreamResetReason::ConnectionTermination
                     : Http::StreamResetReason::LocalConnectionFailure;
  case quic::QUIC_HTTP_FRAME_ERROR:
    return Http::StreamResetReason::ProtocolError;
  default:
    return Http::StreamResetReason::ConnectionTermination;
  }
}

Http::StreamResetReason quicErrorCodeToEnvoyRemoteResetReason(quic::QuicErrorCode error) {
  switch (error) {
  case quic::QUIC_HANDSHAKE_FAILED:
  case quic::QUIC_HANDSHAKE_TIMEOUT:
    return Http::StreamResetReason::RemoteConnectionFailure;
  default:
    return Http::StreamResetReason::ConnectionTermination;
  }
}

Network::ConnectionSocketPtr
createConnectionSocket(const Network::Address::InstanceConstSharedPtr& peer_addr,
                       Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::ConnectionSocket::OptionsSharedPtr& options,
                       const bool prefer_gro) {
  ASSERT(peer_addr != nullptr);
  const bool should_connect =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_connect_client_udp_sockets");
  size_t max_addresses_cache_size =
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_upstream_socket_use_address_cache_for_read")
          ? 4u
          : 0u;

  if (local_addr == nullptr && !should_connect) {
    local_addr = Network::Utility::getLocalAddress(peer_addr->ip()->version());
  }
  auto connection_socket = std::make_unique<Network::ConnectionSocketImpl>(
      Network::Socket::Type::Datagram,
      // Use the loopback address if `local_addr` is null, to pass in the socket interface used to
      // create the IoHandle, without having to make the more expensive `getifaddrs` call.
      local_addr ? local_addr : getLoopbackAddress(peer_addr), peer_addr,
      Network::SocketCreationOptions{false, max_addresses_cache_size});
  connection_socket->setDetectedTransportProtocol("quic");
  if (!connection_socket->isOpen()) {
    ENVOY_LOG_MISC(error, "Failed to create quic socket");
    return connection_socket;
  }
  connection_socket->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
  connection_socket->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_receive_ecn")) {
    connection_socket->addOptions(Network::SocketOptionFactory::buildIpRecvTosOptions());
  }
  if (prefer_gro && Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    connection_socket->addOptions(Network::SocketOptionFactory::buildUdpGroOptions());
  }
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment")) {
    int v6_only = 0;
    if (connection_socket->ipVersion().has_value() &&
        connection_socket->ipVersion().value() == Network::Address::IpVersion::v6) {
      socklen_t v6_only_len = sizeof(v6_only);
      Api::SysCallIntResult result =
          connection_socket->getSocketOption(IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, &v6_only_len);
      if (result.return_value_ != 0) {
        ENVOY_LOG_MISC(
            error, "Failed to get IPV6_V6ONLY socket option, getsockopt() returned {}, errno {}",
            result.return_value_, result.errno_);
        connection_socket->close();
        return connection_socket;
      }
    }
    connection_socket->addOptions(Network::SocketOptionFactory::buildDoNotFragmentOptions(
        /*mapped_v6*/ connection_socket->ipVersion().value() == Network::Address::IpVersion::v6 &&
        v6_only == 0));
  }
  if (options != nullptr) {
    connection_socket->addOptions(options);
  }
  if (!Network::Socket::applyOptions(connection_socket->options(), *connection_socket,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    connection_socket->close();
    ENVOY_LOG_MISC(error, "Fail to apply pre-bind options");
    return connection_socket;
  }

  if (local_addr != nullptr) {
    connection_socket->bind(local_addr);
    ASSERT(local_addr->ip());
  }
  if (should_connect) {
    if (auto result = connection_socket->connect(peer_addr); result.return_value_ == -1) {
      connection_socket->close();
      ENVOY_LOG_MISC(error, "Fail to connect socket: ({}) {}", result.errno_,
                     errorDetails(result.errno_));
      return connection_socket;
    }
  }

  local_addr = connection_socket->connectionInfoProvider().localAddress();
  if (!Network::Socket::applyOptions(connection_socket->options(), *connection_socket,
                                     envoy::config::core::v3::SocketOption::STATE_BOUND)) {
    ENVOY_LOG_MISC(error, "Fail to apply post-bind options");
    connection_socket->close();
  }
  return connection_socket;
}

bssl::UniquePtr<X509> parseDERCertificate(const std::string& der_bytes,
                                          std::string* error_details) {
  const uint8_t* data;
  const uint8_t* orig_data;
  orig_data = data = reinterpret_cast<const uint8_t*>(der_bytes.data());
  bssl::UniquePtr<X509> cert(d2i_X509(nullptr, &data, der_bytes.size()));
  if (!cert.get()) {
    *error_details = "d2i_X509: fail to parse DER";
    return nullptr;
  }
  if (data < orig_data || static_cast<size_t>(data - orig_data) != der_bytes.size()) {
    *error_details = "There is trailing garbage in DER.";
    return nullptr;
  }
  return cert;
}

int deduceSignatureAlgorithmFromPublicKey(const EVP_PKEY* public_key, std::string* error_details) {
  int sign_alg = 0;
  if (public_key == nullptr) {
    *error_details = "Invalid leaf cert, bad public key";
    return sign_alg;
  }
  const int pkey_id = EVP_PKEY_id(public_key);
  switch (pkey_id) {
  case EVP_PKEY_EC: {
    // We only support P-256 ECDSA today.
    const EC_KEY* ecdsa_public_key = EVP_PKEY_get0_EC_KEY(public_key);
    // Since we checked the key type above, this should be valid.
    ASSERT(ecdsa_public_key != nullptr);
    const EC_GROUP* ecdsa_group = EC_KEY_get0_group(ecdsa_public_key);
    if (ecdsa_group == nullptr || EC_GROUP_get_curve_name(ecdsa_group) != NID_X9_62_prime256v1) {
      *error_details = "Invalid leaf cert, only P-256 ECDSA certificates are supported";
      break;
    }
    // QUICHE uses SHA-256 as hash function in cert signature.
    sign_alg = SSL_SIGN_ECDSA_SECP256R1_SHA256;
  } break;
  case EVP_PKEY_RSA: {
    // We require RSA certificates with 2048-bit or larger keys.
    const RSA* rsa_public_key = EVP_PKEY_get0_RSA(public_key);
    // Since we checked the key type above, this should be valid.
    ASSERT(rsa_public_key != nullptr);
    const unsigned rsa_key_length = RSA_size(rsa_public_key);
#ifdef BORINGSSL_FIPS
    if (rsa_key_length != 2048 / 8 && rsa_key_length != 3072 / 8 && rsa_key_length != 4096 / 8) {
      *error_details = "Invalid leaf cert, only RSA certificates with 2048-bit, 3072-bit or "
                       "4096-bit keys are supported in FIPS mode";
      break;
    }
#else
    if (rsa_key_length < 2048 / 8) {
      *error_details =
          "Invalid leaf cert, only RSA certificates with 2048-bit or larger keys are supported";
      break;
    }
#endif
    sign_alg = SSL_SIGN_RSA_PSS_RSAE_SHA256;
  } break;
  default:
    *error_details = "Invalid leaf cert, only RSA and ECDSA certificates are supported";
  }
  return sign_alg;
}

Network::ConnectionSocketPtr
createServerConnectionSocket(Network::IoHandle& io_handle,
                             const quic::QuicSocketAddress& self_address,
                             const quic::QuicSocketAddress& peer_address,
                             const std::string& hostname, absl::string_view alpn) {
  auto connection_socket = std::make_unique<Network::ConnectionSocketImpl>(
      std::make_unique<QuicIoHandleWrapper>(io_handle),
      quicAddressToEnvoyAddressInstance(self_address),
      quicAddressToEnvoyAddressInstance(peer_address));
  connection_socket->setDetectedTransportProtocol("quic");
  connection_socket->setRequestedServerName(hostname);
  connection_socket->setRequestedApplicationProtocols({alpn});
  return connection_socket;
}

void convertQuicConfig(const envoy::config::core::v3::QuicProtocolOptions& config,
                       quic::QuicConfig& quic_config) {
  int32_t max_streams = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_concurrent_streams, 100);
  quic_config.SetMaxBidirectionalStreamsToSend(max_streams);
  quic_config.SetMaxUnidirectionalStreamsToSend(max_streams);
  configQuicInitialFlowControlWindow(config, quic_config);
  quic_config.SetConnectionOptionsToSend(quic::ParseQuicTagVector(config.connection_options()));
  quic_config.SetClientConnectionOptions(
      quic::ParseQuicTagVector(config.client_connection_options()));
  if (config.has_idle_network_timeout()) {
    quic_config.SetIdleNetworkTimeout(quic::QuicTimeDelta::FromSeconds(
        DurationUtil::durationToSeconds(config.idle_network_timeout())));
  }
}

void configQuicInitialFlowControlWindow(const envoy::config::core::v3::QuicProtocolOptions& config,
                                        quic::QuicConfig& quic_config) {
  size_t stream_flow_control_window_to_send = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, initial_stream_window_size,
      Http3::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  if (stream_flow_control_window_to_send < quic::kMinimumFlowControlSendWindow) {
    // If the configured value is smaller than 16kB, only use it for IETF QUIC, because Google QUIC
    // requires minimum 16kB stream flow control window. The QUICHE default 16kB will be used for
    // Google QUIC connections.
    quic_config.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
        stream_flow_control_window_to_send);
  } else {
    // Both Google QUIC and IETF Quic can be configured from this.
    quic_config.SetInitialStreamFlowControlWindowToSend(stream_flow_control_window_to_send);
  }

  uint32_t session_flow_control_window_to_send = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, initial_connection_window_size,
      Http3::Utility::OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
  // Config connection level flow control window shouldn't be smaller than the minimum flow control
  // window supported in QUICHE which is 16kB.
  quic_config.SetInitialSessionFlowControlWindowToSend(
      std::max(quic::kMinimumFlowControlSendWindow,
               static_cast<quic::QuicByteCount>(session_flow_control_window_to_send)));
}

void adjustNewConnectionIdForRouting(quic::QuicConnectionId& new_connection_id,
                                     const quic::QuicConnectionId& old_connection_id) {
  char* new_connection_id_data = new_connection_id.mutable_data();
  const char* old_connection_id_ptr = old_connection_id.data();
  // Override the first 4 bytes of the new CID to the original CID's first 4 bytes.
  memcpy(new_connection_id_data, old_connection_id_ptr, 4); // NOLINT(safe-memcpy)
}

quic::QuicEcnCodepoint getQuicEcnCodepointFromTosByte(uint8_t tos_byte) {
  // Explicit Congestion Notification is encoded in the two least significant
  // bits of the TOS byte of the IP header.
  constexpr uint8_t kEcnMask = 0b00000011;
  return static_cast<quic::QuicEcnCodepoint>(tos_byte & kEcnMask);
}

} // namespace Quic
} // namespace Envoy

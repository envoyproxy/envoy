#pragma once

#include "envoy/common/platform.h"
#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/http/codec.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/quic/quic_io_handle_wrapper.h"

#include "openssl/ssl.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace Envoy {
namespace Quic {

// Changes or additions to details should be reflected in
// docs/root/configuration/http/http_conn_man/response_code_details.rst
class Http3ResponseCodeDetailValues {
public:
  // Invalid HTTP header field was received and stream is going to be
  // closed.
  static constexpr absl::string_view invalid_http_header = "http3.invalid_header_field";
  // The size of headers (or trailers) exceeded the configured limits.
  static constexpr absl::string_view headers_too_large = "http3.headers_too_large";
  // Envoy was configured to drop requests with header keys beginning with underscores.
  static constexpr absl::string_view invalid_underscore = "http3.unexpected_underscore";
  // The peer refused the stream.
  static constexpr absl::string_view remote_refused = "http3.remote_refuse";
  // The peer reset the stream.
  static constexpr absl::string_view remote_reset = "http3.remote_reset";
  // Too many trailers were sent.
  static constexpr absl::string_view too_many_trailers = "http3.too_many_trailers";
  // Too many headers were sent.
  static constexpr absl::string_view too_many_headers = "http3.too_many_headers";
  // The payload size is different from what the content-length header indicated.
  static constexpr absl::string_view inconsistent_content_length =
      "http3.inconsistent_content_length";
};

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address);

quic::QuicSocketAddress envoyIpAddressToQuicSocketAddress(const Network::Address::Ip* envoy_ip);

class HeaderValidator {
public:
  virtual ~HeaderValidator() = default;
  virtual Http::HeaderUtility::HeaderValidationResult
  validateHeader(absl::string_view name, absl::string_view header_value) = 0;
};

// The returned header map has all keys in lower case.
template <class T>
std::unique_ptr<T>
quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list, HeaderValidator& validator,
                          uint32_t max_headers_allowed, absl::string_view& details,
                          quic::QuicRstStreamErrorCode& rst) {
  auto headers = T::create();
  for (const auto& entry : header_list) {
    if (max_headers_allowed == 0) {
      details = Http3ResponseCodeDetailValues::too_many_headers;
      rst = quic::QUIC_STREAM_EXCESSIVE_LOAD;
      return nullptr;
    }
    max_headers_allowed--;
    Http::HeaderUtility::HeaderValidationResult result =
        validator.validateHeader(entry.first, entry.second);
    switch (result) {
    case Http::HeaderUtility::HeaderValidationResult::REJECT:
      rst = quic::QUIC_BAD_APPLICATION_PAYLOAD;
      // The validator sets the details to Http3ResponseCodeDetailValues::invalid_underscore
      return nullptr;
    case Http::HeaderUtility::HeaderValidationResult::DROP:
      continue;
    case Http::HeaderUtility::HeaderValidationResult::ACCEPT:
      auto key = Http::LowerCaseString(entry.first);
      if (key != Http::Headers::get().Cookie) {
        // TODO(danzh): Avoid copy by referencing entry as header_list is already validated by QUIC.
        headers->addCopy(key, entry.second);
      } else {
        // QUICHE breaks "cookie" header into crumbs. Coalesce them by appending current one to
        // existing one if there is any.
        headers->appendCopy(key, entry.second);
      }
    }
  }
  return headers;
}

template <class T>
std::unique_ptr<T>
http2HeaderBlockToEnvoyTrailers(const spdy::Http2HeaderBlock& header_block,
                                uint32_t max_headers_allowed, HeaderValidator& validator,
                                absl::string_view& details, quic::QuicRstStreamErrorCode& rst) {
  auto headers = T::create();
  if (header_block.size() > max_headers_allowed) {
    details = Http3ResponseCodeDetailValues::too_many_trailers;
    rst = quic::QUIC_STREAM_EXCESSIVE_LOAD;
    return nullptr;
  }
  for (auto entry : header_block) {
    // TODO(danzh): Avoid temporary strings and addCopy() with string_view.
    std::string key(entry.first);
    // QUICHE coalesces multiple trailer values with the same key with '\0'.
    std::vector<absl::string_view> values = absl::StrSplit(entry.second, '\0');
    for (const absl::string_view& value : values) {
      if (max_headers_allowed == 0) {
        details = Http3ResponseCodeDetailValues::too_many_trailers;
        rst = quic::QUIC_STREAM_EXCESSIVE_LOAD;
        return nullptr;
      }
      max_headers_allowed--;
      Http::HeaderUtility::HeaderValidationResult result =
          validator.validateHeader(entry.first, value);
      switch (result) {
      case Http::HeaderUtility::HeaderValidationResult::REJECT:
        rst = quic::QUIC_BAD_APPLICATION_PAYLOAD;
        return nullptr;
      case Http::HeaderUtility::HeaderValidationResult::DROP:
        continue;
      case Http::HeaderUtility::HeaderValidationResult::ACCEPT:
        headers->addCopy(Http::LowerCaseString(key), value);
      }
    }
  }
  return headers;
}

spdy::Http2HeaderBlock envoyHeadersToHttp2HeaderBlock(const Http::HeaderMap& headers);

// Called when Envoy wants to reset the underlying QUIC stream.
quic::QuicRstStreamErrorCode envoyResetReasonToQuicRstError(Http::StreamResetReason reason);

// Called when a RST_STREAM frame is received.
Http::StreamResetReason quicRstErrorToEnvoyLocalResetReason(quic::QuicRstStreamErrorCode rst_err);

// Called when a QUIC stack reset the stream.
Http::StreamResetReason quicRstErrorToEnvoyRemoteResetReason(quic::QuicRstStreamErrorCode rst_err);

// Called when underlying QUIC connection is closed locally.
Http::StreamResetReason quicErrorCodeToEnvoyLocalResetReason(quic::QuicErrorCode error,
                                                             bool connected);

// Called when underlying QUIC connection is closed by peer.
Http::StreamResetReason quicErrorCodeToEnvoyRemoteResetReason(quic::QuicErrorCode error);

// Create a connection socket instance and apply given socket options to the
// socket. IP_PKTINFO and SO_RXQ_OVFL is always set if supported.
Network::ConnectionSocketPtr
createConnectionSocket(const Network::Address::InstanceConstSharedPtr& peer_addr,
                       Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::ConnectionSocket::OptionsSharedPtr& options);

// Convert a cert in string form to X509 object.
// Return nullptr if the bytes passed cannot be passed.
bssl::UniquePtr<X509> parseDERCertificate(const std::string& der_bytes, std::string* error_details);

// Deduce the suitable signature algorithm according to the public key.
// Return the sign algorithm id works with the public key; If the public key is
// not supported, return 0 with error_details populated correspondingly.
int deduceSignatureAlgorithmFromPublicKey(const EVP_PKEY* public_key, std::string* error_details);

// Return a connection socket which read and write via io_handle, but doesn't close it when the
// socket gets closed nor set options on the socket.
Network::ConnectionSocketPtr
createServerConnectionSocket(Network::IoHandle& io_handle,
                             const quic::QuicSocketAddress& self_address,
                             const quic::QuicSocketAddress& peer_address,
                             const std::string& hostname, absl::string_view alpn);

// Alter QuicConfig based on all the options in the supplied config.
void convertQuicConfig(const envoy::config::core::v3::QuicProtocolOptions& config,
                       quic::QuicConfig& quic_config);

// Set initial flow control windows in quic_config according to the given Envoy config.
void configQuicInitialFlowControlWindow(const envoy::config::core::v3::QuicProtocolOptions& config,
                                        quic::QuicConfig& quic_config);

// Modify new_connection_id according to given old_connection_id to make sure packets with the new
// one can be routed to the same listener.
void adjustNewConnectionIdForRoutine(quic::QuicConnectionId& new_connection_id,
                                     const quic::QuicConnectionId& old_connection_id);

} // namespace Quic
} // namespace Envoy

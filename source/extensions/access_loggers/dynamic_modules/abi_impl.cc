#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/utility.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/strings/str_split.h"
#include "access_log.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

namespace {

using HeadersMapOptConstRef = OptRef<const Http::HeaderMap>;

HeadersMapOptConstRef getHeaderMapByType(ThreadLocalLogger* logger,
                                         envoy_dynamic_module_type_http_header_type header_type) {
  switch (header_type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return logger->log_context_->requestHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return logger->log_context_->responseHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return logger->log_context_->responseTrailers();
  default:
    return {};
  }
}

bool getHeaderValueImpl(HeadersMapOptConstRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                        size_t* optional_size) {
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);

  // Note: We convert to LowerCaseString which may involve copying. This could be optimized if
  // callers guarantee lowercase keys.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

bool getHeadersImpl(HeadersMapOptConstRef map,
                    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

// Helper to convert MonotonicTime to nanoseconds duration from start time.
int64_t monotonicTimeToNanos(const absl::optional<MonotonicTime>& time,
                             const MonotonicTime& start_time) {
  if (!time.has_value()) {
    return -1;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() - start_time).count();
}

} // namespace

extern "C" {

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Headers
// -----------------------------------------------------------------------------

size_t envoy_dynamic_module_callback_access_logger_get_headers_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  HeadersMapOptConstRef map = getHeaderMapByType(logger, header_type);
  return map.has_value() ? map->size() : 0;
}

bool envoy_dynamic_module_callback_access_logger_get_headers(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  return getHeadersImpl(getHeaderMapByType(logger, header_type), result_headers);
}

bool envoy_dynamic_module_callback_access_logger_get_header_value(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  return getHeaderValueImpl(getHeaderMapByType(logger, header_type), key, result, index,
                            total_count_out);
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Stream Info Basic
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_has_response_flag(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_response_flag flag) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  // Convert ABI flag to Envoy flag. The enum values are expected to match CoreResponseFlag.
  return logger->stream_info_->hasResponseFlag(
      StreamInfo::ResponseFlag(static_cast<StreamInfo::CoreResponseFlag>(flag)));
}

uint64_t envoy_dynamic_module_callback_access_logger_get_response_flags(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  return logger->stream_info_->legacyResponseFlags();
}

void envoy_dynamic_module_callback_access_logger_get_timing_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_timing_info* timing_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& info = *logger->stream_info_;
  const MonotonicTime start_time = info.startTimeMonotonic();

  timing_out->start_time_unix_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(info.startTime().time_since_epoch())
          .count();

  auto duration = info.requestComplete();
  timing_out->request_complete_duration_ns = duration.has_value() ? duration->count() : -1;

  // Downstream timing.
  const auto downstream = info.downstreamTiming();
  if (downstream.has_value()) {
    timing_out->first_downstream_tx_byte_sent_ns =
        monotonicTimeToNanos(downstream->firstDownstreamTxByteSent(), start_time);
    timing_out->last_downstream_tx_byte_sent_ns =
        monotonicTimeToNanos(downstream->lastDownstreamTxByteSent(), start_time);
  } else {
    timing_out->first_downstream_tx_byte_sent_ns = -1;
    timing_out->last_downstream_tx_byte_sent_ns = -1;
  }

  // Upstream timing.
  const auto upstream = info.upstreamInfo();
  if (upstream.has_value()) {
    const auto& upstream_timing = upstream->upstreamTiming();
    timing_out->first_upstream_tx_byte_sent_ns =
        monotonicTimeToNanos(upstream_timing.first_upstream_tx_byte_sent_, start_time);
    timing_out->last_upstream_tx_byte_sent_ns =
        monotonicTimeToNanos(upstream_timing.last_upstream_tx_byte_sent_, start_time);
    timing_out->first_upstream_rx_byte_received_ns =
        monotonicTimeToNanos(upstream_timing.first_upstream_rx_byte_received_, start_time);
    timing_out->last_upstream_rx_byte_received_ns =
        monotonicTimeToNanos(upstream_timing.last_upstream_rx_byte_received_, start_time);
  } else {
    timing_out->first_upstream_tx_byte_sent_ns = -1;
    timing_out->last_upstream_tx_byte_sent_ns = -1;
    timing_out->first_upstream_rx_byte_received_ns = -1;
    timing_out->last_upstream_rx_byte_received_ns = -1;
  }
}

void envoy_dynamic_module_callback_access_logger_get_bytes_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_bytes_info* bytes_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& info = *logger->stream_info_;
  bytes_out->bytes_received = info.bytesReceived();
  bytes_out->bytes_sent = info.bytesSent();

  const auto& upstream = info.getUpstreamBytesMeter();
  if (upstream) {
    bytes_out->wire_bytes_received = upstream->wireBytesReceived();
    bytes_out->wire_bytes_sent = upstream->wireBytesSent();
  } else {
    bytes_out->wire_bytes_received = 0;
    bytes_out->wire_bytes_sent = 0;
  }
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Address Information
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.remoteAddress() || provider.remoteAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.remoteAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.localAddress() || provider.localAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.localAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.directRemoteAddress() ||
      provider.directRemoteAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.directRemoteAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.directLocalAddress() ||
      provider.directLocalAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.directLocalAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamHost() || !upstream->upstreamHost()->address()) {
    return false;
  }

  const auto& address = upstream->upstreamHost()->address();
  if (address->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = address->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamLocalAddress()) {
    return false;
  }

  const auto& address = upstream->upstreamLocalAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = address->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Upstream Info
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_upstream_cluster(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  // upstreamClusterInfo is on StreamInfo, not UpstreamInfo.
  const auto cluster_info = logger->stream_info_->upstreamClusterInfo();
  if (!cluster_info) {
    return false;
  }

  const auto& name = cluster_info->name();
  *result = {const_cast<char*>(name.data()), name.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_host(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamHost()) {
    return false;
  }

  const auto& hostname = upstream->upstreamHost()->hostname();
  *result = {const_cast<char*>(hostname.data()), hostname.size()};
  return true;
}

uint64_t envoy_dynamic_module_callback_access_logger_get_upstream_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamConnectionId().has_value()) {
    return 0;
  }
  return upstream->upstreamConnectionId().value();
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // ciphersuiteString() returns std::string by value, so we use thread-local storage.
  static thread_local std::string tls_cipher_str;
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }

  tls_cipher_str = upstream->upstreamSslConnection()->ciphersuiteString();
  if (tls_cipher_str.empty()) {
    return false;
  }
  *result = {const_cast<char*>(tls_cipher_str.data()), tls_cipher_str.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }

  const std::string& session_id = upstream->upstreamSslConnection()->sessionId();
  if (session_id.empty()) {
    return false;
  }
  *result = {const_cast<char*>(session_id.data()), session_id.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }

  const std::string& issuer = upstream->upstreamSslConnection()->issuerPeerCertificate();
  if (issuer.empty()) {
    return false;
  }
  *result = {const_cast<char*>(issuer.data()), issuer.size()};
  return true;
}

int64_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  const auto valid_from = upstream->upstreamSslConnection()->validFromPeerCertificate();
  if (!valid_from.has_value()) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(valid_from->time_since_epoch()).count();
}

int64_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  const auto expiration = upstream->upstreamSslConnection()->expirationPeerCertificate();
  if (!expiration.has_value()) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(expiration->time_since_epoch()).count();
}

size_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  return upstream->upstreamSslConnection()->uriSanPeerCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const auto& sans = upstream->upstreamSslConnection()->uriSanPeerCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  return upstream->upstreamSslConnection()->uriSanLocalCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const auto& sans = upstream->upstreamSslConnection()->uriSanLocalCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  return upstream->upstreamSslConnection()->dnsSansPeerCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const auto& sans = upstream->upstreamSslConnection()->dnsSansPeerCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return 0;
  }
  return upstream->upstreamSslConnection()->dnsSansLocalCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const auto& sans = upstream->upstreamSslConnection()->dnsSansLocalCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Connection/TLS Info
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // ciphersuiteString() returns std::string by value, so we use thread-local storage.
  static thread_local std::string tls_cipher_str;
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  tls_cipher_str = provider.sslConnection()->ciphersuiteString();
  if (tls_cipher_str.empty()) {
    return false;
  }
  *result = {const_cast<char*>(tls_cipher_str.data()), tls_cipher_str.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& session_id = provider.sslConnection()->sessionId();
  if (session_id.empty()) {
    return false;
  }
  *result = {const_cast<char*>(session_id.data()), session_id.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& issuer = provider.sslConnection()->issuerPeerCertificate();
  if (issuer.empty()) {
    return false;
  }
  *result = {const_cast<char*>(issuer.data()), issuer.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& serial = provider.sslConnection()->serialNumberPeerCertificate();
  if (serial.empty()) {
    return false;
  }
  *result = {const_cast<char*>(serial.data()), serial.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& digest = provider.sslConnection()->sha1PeerCertificateDigest();
  if (digest.empty()) {
    return false;
  }
  *result = {const_cast<char*>(digest.data()), digest.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  return provider.sslConnection()->peerCertificatePresented();
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  return provider.sslConnection()->peerCertificateValidated();
}

int64_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  const auto valid_from = provider.sslConnection()->validFromPeerCertificate();
  if (!valid_from.has_value()) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(valid_from->time_since_epoch()).count();
}

int64_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  const auto expiration = provider.sslConnection()->expirationPeerCertificate();
  if (!expiration.has_value()) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::seconds>(expiration->time_since_epoch()).count();
}

size_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  return provider.sslConnection()->uriSanPeerCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const auto& sans = provider.sslConnection()->uriSanPeerCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  return provider.sslConnection()->uriSanLocalCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const auto& sans = provider.sslConnection()->uriSanLocalCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  return provider.sslConnection()->dnsSansPeerCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const auto& sans = provider.sslConnection()->dnsSansPeerCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

size_t envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return 0;
  }
  return provider.sslConnection()->dnsSansLocalCertificate().size();
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const auto& sans = provider.sslConnection()->dnsSansLocalCertificate();
  for (size_t i = 0; i < sans.size(); ++i) {
    sans_out[i].ptr = const_cast<char*>(sans[i].data());
    sans_out[i].length = sans[i].size();
  }
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Metadata and Dynamic State
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  std::string filter_name_str(filter_name.ptr, filter_name.length);
  std::string path_str(path.ptr, path.length);
  std::vector<std::string> path_parts = absl::StrSplit(path_str, '.');

  const auto& metadata = logger->stream_info_->dynamicMetadata();
  const auto& value =
      Envoy::Config::Metadata::metadataValue(&metadata, filter_name_str, path_parts);

  if (value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    return false;
  }

  // Note: Currently only string values are supported. Complex types would require serialization
  // to a buffer, but the ABI uses zero-copy pointers to Envoy memory.
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    const auto& str = value.string_value();
    *result = {const_cast<char*>(str.data()), str.size()};
    return true;
  }

  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_filter_state(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: FilterState access is not currently supported. FilterState serialization requires
  // allocation, but the ABI uses zero-copy pointers.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(key);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_local_reply_body(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  absl::string_view body = logger->log_context_->localReplyBody();
  if (body.empty()) {
    return false;
  }
  *result = {const_cast<char*>(body.data()), body.size()};
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Tracing
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_trace_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: Tracing span access is not currently supported. The Span interface doesn't expose
  // trace/span IDs in a way that allows zero-copy access.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_span_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: Tracing span access is not currently supported. The Span interface doesn't expose
  // trace/span IDs in a way that allows zero-copy access.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_is_trace_sampled(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  // Note: The Span interface doesn't expose a sampled() method. We check trace reason instead.
  return logger->stream_info_->traceReason() != Tracing::Reason::NotTraceable;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Additional Stream Info
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_ja3_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  const auto& hash = provider.ja3Hash();
  if (hash.empty()) {
    return false;
  }
  *result = {const_cast<char*>(hash.data()), hash.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_ja4_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  const auto& hash = provider.ja4Hash();
  if (hash.empty()) {
    return false;
  }
  *result = {const_cast<char*>(hash.data()), hash.size()};
  return true;
}

uint64_t envoy_dynamic_module_callback_access_logger_get_request_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& headers = logger->log_context_->requestHeaders();
  return headers.has_value() ? headers->byteSize() : 0;
}

uint64_t envoy_dynamic_module_callback_access_logger_get_response_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& headers = logger->log_context_->responseHeaders();
  return headers.has_value() ? headers->byteSize() : 0;
}

uint64_t envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto& trailers = logger->log_context_->responseTrailers();
  return trailers.has_value() ? trailers->byteSize() : 0;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamProtocol().has_value()) {
    return false;
  }
  const auto& protocol_str = Http::Utility::getProtocolString(upstream->upstreamProtocol().value());
  *result = {const_cast<char*>(protocol_str.data()), protocol_str.size()};
  return true;
}

int64_t envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value()) {
    return -1;
  }
  const auto& latency = upstream->upstreamTiming().connectionPoolCallbackLatency();
  if (!latency.has_value()) {
    return -1;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(latency.value()).count();
}

// -----------------------------------------------------------------------------
// Generic Attribute Accessors
// -----------------------------------------------------------------------------

// Helper to extract a downstream SSL string attribute from the access log context.
bool getDownstreamSslAttribute(
    ThreadLocalLogger* logger,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> extractor,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& provider = logger->stream_info_->downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = provider.sslConnection();
  OptRef<const std::string> attr = extractor(ssl);
  if (!attr.has_value() || attr->empty()) {
    return false;
  }
  const std::string& value = attr.value();
  *result = {const_cast<char*>(value.data()), value.size()};
  return true;
}

// Helper to extract an upstream SSL string attribute from the access log context.
bool getUpstreamSslAttribute(
    ThreadLocalLogger* logger,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> extractor,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto upstream = logger->stream_info_->upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamSslConnection()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = upstream->upstreamSslConnection();
  OptRef<const std::string> attr = extractor(ssl);
  if (!attr.has_value() || attr->empty()) {
    return false;
  }
  const std::string& value = attr.value();
  *result = {const_cast<char*>(value.data()), value.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_attribute_string(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_RequestProtocol: {
    if (!logger->stream_info_->protocol().has_value()) {
      break;
    }
    const auto& protocol_str =
        Http::Utility::getProtocolString(logger->stream_info_->protocol().value());
    *result = {const_cast<char*>(protocol_str.data()), protocol_str.size()};
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ResponseCodeDetails: {
    if (!logger->stream_info_->responseCodeDetails().has_value()) {
      break;
    }
    const auto& details = logger->stream_info_->responseCodeDetails().value();
    *result = {const_cast<char*>(details.data()), details.size()};
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsRouteName: {
    const auto& name = logger->stream_info_->getRouteName();
    if (!name.empty()) {
      *result = {const_cast<char*>(name.data()), name.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsVirtualHostName: {
    const auto& name = logger->stream_info_->virtualClusterName();
    if (name.has_value() && !name->empty()) {
      *result = {const_cast<char*>(name->data()), name->size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestId: {
    const auto provider = logger->stream_info_->getStreamIdProvider();
    if (provider.has_value() && provider->toStringView().has_value()) {
      absl::string_view view = provider->toStringView().value();
      *result = {const_cast<char*>(view.data()), view.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourceAddress: {
    const auto& addr_provider = logger->stream_info_->downstreamAddressProvider();
    if (addr_provider.remoteAddress() &&
        addr_provider.remoteAddress()->type() == Network::Address::Type::Ip) {
      const auto& addr_str = addr_provider.remoteAddress()->ip()->addressAsString();
      *result = {const_cast<char*>(addr_str.data()), addr_str.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationAddress: {
    const auto& addr_provider = logger->stream_info_->downstreamAddressProvider();
    if (addr_provider.localAddress() &&
        addr_provider.localAddress()->type() == Network::Address::Type::Ip) {
      const auto& addr_str = addr_provider.localAddress()->ip()->addressAsString();
      *result = {const_cast<char*>(addr_str.data()), addr_str.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionRequestedServerName: {
    const auto& sni = logger->stream_info_->downstreamAddressProvider().requestedServerName();
    if (!sni.empty()) {
      *result = {const_cast<char*>(sni.data()), sni.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTerminationDetails: {
    const auto& details = logger->stream_info_->connectionTerminationDetails();
    if (details.has_value() && !details->empty()) {
      *result = {const_cast<char*>(details->data()), details->size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTransportFailureReason: {
    const auto& reason = logger->stream_info_->downstreamTransportFailureReason();
    if (!reason.empty()) {
      *result = {const_cast<char*>(reason.data()), reason.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamAddress: {
    const auto upstream = logger->stream_info_->upstreamInfo();
    if (upstream.has_value() && upstream->upstreamHost() &&
        upstream->upstreamHost()->address() != nullptr) {
      auto addr = upstream->upstreamHost()->address()->asStringView();
      *result = {const_cast<char*>(addr.data()), addr.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamLocalAddress: {
    const auto upstream = logger->stream_info_->upstreamInfo();
    if (upstream.has_value() && upstream->upstreamLocalAddress() != nullptr) {
      auto addr = upstream->upstreamLocalAddress()->asStringView();
      *result = {const_cast<char*>(addr.data()), addr.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamTransportFailureReason: {
    const auto upstream = logger->stream_info_->upstreamInfo();
    if (upstream.has_value() && !upstream->upstreamTransportFailureReason().empty()) {
      const auto& reason = upstream->upstreamTransportFailureReason();
      *result = {const_cast<char*>(reason.data()), reason.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->tlsVersion();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate:
    return getDownstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamTlsVersion:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->tlsVersion();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSubjectPeerCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSubjectLocalCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamSha256PeerCertificateDigest:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamDnsSanLocalCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamDnsSanPeerCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamUriSanLocalCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_UpstreamUriSanPeerCertificate:
    return getUpstreamSslAttribute(
        logger,
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result);
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as string for access logger.",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool envoy_dynamic_module_callback_access_logger_get_attribute_int(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ResponseCode: {
    const auto code = logger->stream_info_->responseCode();
    if (code.has_value()) {
      *result = code.value();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionId: {
    *result = logger->stream_info_->downstreamAddressProvider().connectionID().value_or(0);
    ok = true;
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourcePort: {
    const auto& addr = logger->stream_info_->downstreamAddressProvider().remoteAddress();
    if (addr && addr->type() == Network::Address::Type::Ip) {
      *result = addr->ip()->port();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationPort: {
    const auto& addr = logger->stream_info_->downstreamAddressProvider().localAddress();
    if (addr && addr->type() == Network::Address::Type::Ip) {
      *result = addr->ip()->port();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamPort: {
    const auto upstream = logger->stream_info_->upstreamInfo();
    if (upstream.has_value() && upstream->upstreamHost() &&
        upstream->upstreamHost()->address() != nullptr) {
      auto ip = upstream->upstreamHost()->address()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamRequestAttemptCount: {
    *result = logger->stream_info_->attemptCount().value_or(0);
    ok = true;
    break;
  }
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as int for access logger.",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool envoy_dynamic_module_callback_access_logger_get_attribute_bool(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ConnectionMtls: {
    const auto& provider = logger->stream_info_->downstreamAddressProvider();
    if (provider.sslConnection()) {
      *result = provider.sslConnection()->peerCertificatePresented();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_HealthCheck:
    *result = logger->stream_info_->healthCheck();
    ok = true;
    break;
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "Unsupported attribute ID {} as bool for access logger.",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

// -----------------------------------------------------------------------------
// Deprecated ABI Wrappers
// -----------------------------------------------------------------------------
// These functions are deprecated and delegate to the generic attribute accessors.
// They are kept for backward compatibility with modules compiled against the
// previous ABI version (1.37). Use the generic attribute accessors instead.

uint32_t envoy_dynamic_module_callback_access_logger_get_response_code(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  uint64_t result = 0;
  if (envoy_dynamic_module_callback_access_logger_get_attribute_int(
          logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ResponseCode, &result)) {
    return static_cast<uint32_t>(result);
  }
  return 0;
}

bool envoy_dynamic_module_callback_access_logger_get_response_code_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ResponseCodeDetails, result);
}

bool envoy_dynamic_module_callback_access_logger_get_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_RequestProtocol, result);
}

bool envoy_dynamic_module_callback_access_logger_get_route_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_XdsRouteName, result);
}

bool envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_XdsVirtualHostName, result);
}

uint32_t envoy_dynamic_module_callback_access_logger_get_attempt_count(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  uint64_t result = 0;
  envoy_dynamic_module_callback_access_logger_get_attribute_int(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamRequestAttemptCount,
      &result);
  return static_cast<uint32_t>(result);
}

bool envoy_dynamic_module_callback_access_logger_get_connection_termination_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionTerminationDetails,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamTransportFailureReason,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamTlsVersion, result);
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamSubjectPeerCertificate,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamSubjectLocalCertificate,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_UpstreamSha256PeerCertificateDigest,
      result);
}

uint64_t envoy_dynamic_module_callback_access_logger_get_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  uint64_t result = 0;
  envoy_dynamic_module_callback_access_logger_get_attribute_int(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionId, &result);
  return result;
}

bool envoy_dynamic_module_callback_access_logger_is_mtls(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  bool result = false;
  if (envoy_dynamic_module_callback_access_logger_get_attribute_bool(
          logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionMtls, &result)) {
    return result;
  }
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_requested_server_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionRequestedServerName,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion, result);
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr,
      envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest, result);
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate,
      result);
}

bool envoy_dynamic_module_callback_access_logger_get_request_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_RequestId, result);
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return envoy_dynamic_module_callback_access_logger_get_attribute_string(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_ConnectionTransportFailureReason,
      result);
}

bool envoy_dynamic_module_callback_access_logger_is_health_check(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  bool result = false;
  envoy_dynamic_module_callback_access_logger_get_attribute_bool(
      logger_envoy_ptr, envoy_dynamic_module_type_attribute_id_HealthCheck, &result);
  return result;
}

// -----------------------------------------------------------------------------
// Metrics Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Counter& c = Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto counter = config->getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*config->stats_scope_, {main_stat_name},
                                                       Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_access_logger_set_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_decrement_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_histogram(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Histogram& h = Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_record_histogram_value(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto histogram = config->getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -----------------------------------------------------------------------------
// Misc Callbacks
// -----------------------------------------------------------------------------
uint32_t envoy_dynamic_module_callback_access_logger_get_worker_index(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* logger = static_cast<ThreadLocalLogger*>(logger_envoy_ptr);
  return logger->worker_index_;
}

} // extern "C"

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy

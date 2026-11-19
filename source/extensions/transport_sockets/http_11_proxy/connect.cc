#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

#include <sstream>

#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

bool UpstreamHttp11ConnectSocket::isValidConnectResponse(absl::string_view response_payload,
                                                         bool& headers_complete,
                                                         size_t& bytes_processed) {
  SelfContainedParser parser;

  bytes_processed = parser.parser().execute(response_payload.data(), response_payload.length());
  headers_complete = parser.headersComplete();

  return parser.parser().getStatus() != Http::Http1::ParserStatus::Error &&
         parser.headersComplete() && parser.parser().statusCode() == Http::Code::OK;
}

UpstreamHttp11ConnectSocket::UpstreamHttp11ConnectSocket(
    Network::TransportSocketPtr&& transport_socket,
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host,
    std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info)
    : PassthroughSocket(std::move(transport_socket)), options_(options) {
  // If the filter state metadata has populated the relevant entries in the transport socket
  // options, we want to maintain the original behavior of this transport socket.
  if (options_ && options_->http11ProxyInfo()) {
    handleProxyInfoConnect(options_->http11ProxyInfo().value());
    return;
  }

  if (proxy_info.has_value()) {
    Network::TransportSocketOptions::Http11ProxyInfo actual_info = proxy_info.value();
    if (actual_info.hostname.empty() && host) {
      actual_info.hostname = host->hostname().empty() ? host->address()->asStringView()
                                                      : absl::StrCat(host->hostname(), ":",
                                                                     host->address()->ip()->port());
    }
    handleProxyInfoConnect(actual_info);
    return;
  }

  // The absence of proxy info from the transport socket options means that we should use the host
  // address of the provided HostDescription if it has the appropriate metadata set.
  for (auto& metadata : {host->metadata(), host->localityMetadata()}) {
    if (metadata == nullptr) {
      continue;
    }

    const bool has_proxy_addr = metadata->typed_filter_metadata().contains(
        Config::MetadataFilters::get().ENVOY_HTTP11_PROXY_TRANSPORT_SOCKET_ADDR);
    if (has_proxy_addr) {
      handleHostMetadataConnect(host);
      break;
    }
  }
}

// Helper method to create a properly formatted CONNECT request with Host header.
std::string UpstreamHttp11ConnectSocket::formatConnectRequest(absl::string_view target) {
  return absl::StrCat("CONNECT ", target, " HTTP/1.1\r\n", "Host: ", target, "\r\n\r\n");
}

inline void UpstreamHttp11ConnectSocket::handleProxyInfoConnect(
    const Network::TransportSocketOptions::Http11ProxyInfo& proxy_info) {
  if (transport_socket_->ssl()) {
    std::string target = absl::StrCat(
        proxy_info.hostname, Http::HeaderUtility::hostHasPort(proxy_info.hostname) ? "" : ":443");

    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.http_11_proxy_connect_legacy_format")) {
      // RFC 9110 compliant CONNECT format that includes Host header.
      header_buffer_.add(formatConnectRequest(target));
    } else {
      // Legacy behavior: no Host header for backward compatibility.
      header_buffer_.add(absl::StrCat("CONNECT ", target, " HTTP/1.1\r\n\r\n"));
    }
    need_to_strip_connect_response_ = true;
  }
}

inline void UpstreamHttp11ConnectSocket::handleHostMetadataConnect(
    std::shared_ptr<const Upstream::HostDescription> host) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http_11_proxy_connect_legacy_format")) {
    // Prefer <host-name>:<port> for RFC 9110 compliance, unless URI is <host-ip>:<port>.
    std::string target;
    if (!host->hostname().empty()) {
      const uint32_t port = host->address()->ip()->port();
      target = absl::StrCat(host->hostname(), ":", port);
    } else {
      target = host->address()->asStringView();
    }
    header_buffer_.add(formatConnectRequest(target));
  } else {
    // Legacy behavior: <host-ip>:<port> format, no Host header for backward compatibility.
    header_buffer_.add(
        absl::StrCat("CONNECT ", host->address()->asStringView(), " HTTP/1.1\r\n\r\n"));
  }
  need_to_strip_connect_response_ = true;
}

void UpstreamHttp11ConnectSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
}

Network::IoResult UpstreamHttp11ConnectSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (header_buffer_.length() > 0) {
    return writeHeader();
  }
  if (!need_to_strip_connect_response_) {
    // Don't pass events up until the connect response is read because TLS reads
    // kick off writes which don't pass through the transport socket.
    return transport_socket_->doWrite(buffer, end_stream);
  }
  return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
}

Network::IoResult UpstreamHttp11ConnectSocket::doRead(Buffer::Instance& buffer) {
  if (need_to_strip_connect_response_) {
    // Limit the CONNECT response headers to an arbitrary 2000 bytes.
    constexpr uint32_t MAX_RESPONSE_HEADER_SIZE = 2000;
    char peek_buf[MAX_RESPONSE_HEADER_SIZE];
    Api::IoCallUint64Result result =
        callbacks_->ioHandle().recv(peek_buf, MAX_RESPONSE_HEADER_SIZE, MSG_PEEK);
    if (!result.ok() && result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      return {Network::PostIoAction::Close, 0, false};
    }
    absl::string_view peek_data(peek_buf, result.return_value_);
    size_t bytes_processed = 0;
    bool headers_complete = false;
    bool is_valid_connect_response =
        isValidConnectResponse(peek_data, headers_complete, bytes_processed);

    if (!headers_complete) {
      if (peek_data.size() == MAX_RESPONSE_HEADER_SIZE) {
        ENVOY_CONN_LOG(trace, "failed to receive CONNECT headers within {} bytes",
                       callbacks_->connection(), MAX_RESPONSE_HEADER_SIZE);
        return {Network::PostIoAction::Close, 0, false};
      }
      ENVOY_CONN_LOG(trace, "Incomplete CONNECT header: {} bytes received",
                     callbacks_->connection(), peek_data.size());
      return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
    }
    if (!is_valid_connect_response) {
      ENVOY_CONN_LOG(trace, "Response does not appear to be a successful CONNECT upgrade",
                     callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }

    result = callbacks_->ioHandle().read(buffer, bytes_processed);
    if (!result.ok() || result.return_value_ != bytes_processed) {
      ENVOY_CONN_LOG(trace, "failed to drain CONNECT header", callbacks_->connection());
      return {Network::PostIoAction::Close, 0, false};
    }
    buffer.drain(bytes_processed);

    ENVOY_CONN_LOG(trace, "Successfully stripped {} bytes of CONNECT header",
                   callbacks_->connection(), bytes_processed);
    need_to_strip_connect_response_ = false;
    callbacks_->flushWriteBuffer();
  }
  return transport_socket_->doRead(buffer);
}

Network::IoResult UpstreamHttp11ConnectSocket::writeHeader() {
  Network::PostIoAction action = Network::PostIoAction::KeepOpen;
  uint64_t bytes_written = 0;
  do {
    if (header_buffer_.length() == 0) {
      break;
    }

    Api::IoCallUint64Result result = callbacks_->ioHandle().write(header_buffer_);

    if (!result.ok()) {
      ENVOY_CONN_LOG(trace, "Failed writing CONNECT header. write error: {}",
                     callbacks_->connection(), result.err_->getErrorDetails());
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = Network::PostIoAction::Close;
      }
      break;
    }
    ENVOY_CONN_LOG(trace, "Writing CONNECT header. write returned: {}", callbacks_->connection(),
                   result.return_value_);
    bytes_written += result.return_value_;
  } while (true);

  return {action, bytes_written, false};
}

std::optional<std::string> UpstreamHttp11TunnelSocket::connectTarget(
    const Network::TransportSocketOptionsConstSharedPtr& options,
    const Upstream::HostDescription* host, bool has_default_proxy, bool origin_is_tls) {
  if (options && options->http11ProxyInfo().has_value()) {
    const std::string& hostname = options->http11ProxyInfo()->hostname;
    if (Http::HeaderUtility::hostHasPort(hostname)) {
      return hostname;
    }
    return absl::StrCat(hostname, origin_is_tls ? ":443" : ":80");
  }
  if (host == nullptr) {
    return std::nullopt;
  }

  bool proxied = has_default_proxy;
  for (const auto& metadata : {host->metadata(), host->localityMetadata()}) {
    if (metadata != nullptr &&
        metadata->typed_filter_metadata().contains(
            Config::MetadataFilters::get().ENVOY_HTTP11_PROXY_TRANSPORT_SOCKET_ADDR)) {
      proxied = true;
      break;
    }
  }
  if (!proxied) {
    return std::nullopt;
  }
  // Prefer <host-name>:<port> for RFC 9110 compliance, unless only the address is known.
  if (!host->hostname().empty()) {
    return absl::StrCat(host->hostname(), ":", host->address()->ip()->port());
  }
  return std::string(host->address()->asStringView());
}

std::string
UpstreamHttp11TunnelSocket::formatConnectRequest(absl::string_view target,
                                                 absl::string_view proxy_authorization) {
  if (proxy_authorization.empty()) {
    return UpstreamHttp11ConnectSocket::formatConnectRequest(target);
  }
  return absl::StrCat("CONNECT ", target, " HTTP/1.1\r\n", "Host: ", target, "\r\n",
                      "Proxy-Authorization: ", proxy_authorization, "\r\n\r\n");
}

UpstreamHttp11TunnelSocket::UpstreamHttp11TunnelSocket(Network::TransportSocketPtr&& origin_socket,
                                                       Network::TransportSocketPtr&& proxy_socket,
                                                       absl::string_view target,
                                                       absl::string_view proxy_authorization)
    : origin_socket_(std::move(origin_socket)), proxy_socket_(std::move(proxy_socket)) {
  if (!Http::HeaderUtility::headerValueIsValid(proxy_authorization)) {
    // Never send an invalid header value (e.g. one containing CR/LF) to the proxy.
    failure_reason_ = "invalid proxy authorization credential";
    return;
  }
  connect_request_.add(formatConnectRequest(target, proxy_authorization));
}

void UpstreamHttp11TunnelSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  tunnel_io_handle_ = std::make_unique<TunnelIoHandle>(callbacks.ioHandle());
  proxy_socket_->setTransportSocketCallbacks(proxy_callbacks_);
  origin_socket_->setTransportSocketCallbacks(tunnel_callbacks_);
}

bool UpstreamHttp11TunnelSocket::ProxyCallbacks::shouldDrainReadBuffer() {
  // Bound the amount of data buffered from the proxy and not yet consumed by the origin socket.
  const uint32_t limit = parent_.callbacks_->connection().bufferLimit();
  return parent_.callbacks_->shouldDrainReadBuffer() ||
         (limit > 0 && parent_.tunnel_io_handle_->inbound().length() >= limit);
}

void UpstreamHttp11TunnelSocket::ProxyCallbacks::raiseEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    // The connection is not connected until the origin socket is connected through the tunnel.
    ENVOY_CONN_LOG(trace, "connected to the proxy", parent_.callbacks_->connection());
    return;
  }
  parent_.callbacks_->raiseEvent(event);
}

absl::string_view UpstreamHttp11TunnelSocket::failureReason() const {
  if (!failure_reason_.empty()) {
    return failure_reason_;
  }
  if (!proxy_socket_->failureReason().empty()) {
    return proxy_socket_->failureReason();
  }
  return origin_socket_->failureReason();
}

void UpstreamHttp11TunnelSocket::closeSocket(Network::ConnectionEvent event, bool abort_reset) {
  origin_socket_->closeSocket(event, abort_reset);
  if (tunnel_established_ && !abort_reset) {
    // Best effort attempt to send any final data (e.g. a TLS close_notify alert) to the origin.
    flushOutbound();
  }
  proxy_socket_->closeSocket(event, abort_reset);
}

Network::IoResult UpstreamHttp11TunnelSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (!failure_reason_.empty()) {
    return {Network::PostIoAction::Close, 0, false};
  }
  if (!tunnel_established_) {
    // Drive the connection to the proxy (e.g. the TLS handshake) and send the CONNECT request.
    // Data from the origin socket is held until the tunnel is established.
    Network::IoResult result = proxy_socket_->doWrite(connect_request_, false);
    return {result.action_, 0, false, result.err_code_};
  }

  Network::IoResult origin_result = origin_socket_->doWrite(buffer, end_stream);
  Network::IoResult proxy_result = flushOutbound();
  if (proxy_result.action_ == Network::PostIoAction::Close) {
    return {Network::PostIoAction::Close, origin_result.bytes_processed_, false,
            proxy_result.err_code_};
  }
  return origin_result;
}

Network::IoResult UpstreamHttp11TunnelSocket::flushOutbound() {
  Buffer::Instance& outbound = tunnel_io_handle_->outbound();
  const bool end_stream = tunnel_io_handle_->writeShutdown();
  if (outbound.length() == 0 && !end_stream) {
    return {Network::PostIoAction::KeepOpen, 0, false};
  }
  return proxy_socket_->doWrite(outbound, end_stream);
}

Network::IoResult UpstreamHttp11TunnelSocket::doRead(Buffer::Instance& buffer) {
  if (!failure_reason_.empty()) {
    return {Network::PostIoAction::Close, 0, false};
  }
  if (!proxy_end_stream_) {
    Network::IoResult proxy_result = proxy_socket_->doRead(tunnel_io_handle_->inbound());
    if (proxy_result.action_ == Network::PostIoAction::Close) {
      return {Network::PostIoAction::Close, 0, false, proxy_result.err_code_};
    }
    if (proxy_result.end_stream_read_) {
      proxy_end_stream_ = true;
      tunnel_io_handle_->setReadEndStream();
    }
  }

  if (!tunnel_established_) {
    // A read may have completed the handshake with the proxy, so try to send the CONNECT request.
    if (connect_request_.length() > 0) {
      Network::IoResult result = proxy_socket_->doWrite(connect_request_, false);
      if (result.action_ == Network::PostIoAction::Close) {
        return {Network::PostIoAction::Close, 0, false, result.err_code_};
      }
    }
    if (!processConnectResponse()) {
      return {Network::PostIoAction::Close, 0, false};
    }
    if (!tunnel_established_) {
      if (proxy_end_stream_) {
        failure_reason_ = "proxy closed the connection before responding to CONNECT";
        return {Network::PostIoAction::Close, 0, false};
      }
      return {Network::PostIoAction::KeepOpen, 0, false};
    }
    if (socketClosed()) {
      // The connection may have been closed while raising the Connected event.
      return {Network::PostIoAction::Close, 0, false};
    }
  }

  Network::IoResult origin_result = origin_socket_->doRead(buffer);
  // Reading may produce data to be sent to the origin (e.g. TLS handshake messages).
  Network::IoResult proxy_result = flushOutbound();
  if (proxy_result.action_ == Network::PostIoAction::Close) {
    return {Network::PostIoAction::Close, origin_result.bytes_processed_, false,
            proxy_result.err_code_};
  }
  if (origin_result.action_ == Network::PostIoAction::KeepOpen &&
      tunnel_io_handle_->inbound().length() > 0) {
    // The origin socket stopped reading before consuming all the data received from the proxy.
    callbacks_->setTransportSocketIsReadable();
  }
  return origin_result;
}

bool UpstreamHttp11TunnelSocket::processConnectResponse() {
  Buffer::Instance& inbound = tunnel_io_handle_->inbound();
  if (connect_request_.length() > 0 || inbound.length() == 0) {
    // The CONNECT request has not been fully sent yet, or no response data is available.
    return true;
  }

  const uint64_t length = std::min<uint64_t>(inbound.length(), MaxResponseHeaderSize);
  absl::string_view data(static_cast<const char*>(inbound.linearize(length)), length);
  SelfContainedParser parser;
  const size_t bytes_processed = parser.parser().execute(data.data(), data.length());
  // The parser fails on headers exceeding MaxResponseHeaderSize. Also fail if the headers are
  // incomplete after reading that many bytes, so the inbound buffer cannot grow unbounded.
  if (parser.parser().getStatus() == Http::Http1::ParserStatus::Error ||
      (!parser.headersComplete() && length == MaxResponseHeaderSize)) {
    failure_reason_ = "invalid response to CONNECT from proxy";
    return false;
  }
  if (!parser.headersComplete()) {
    ENVOY_CONN_LOG(trace, "Incomplete CONNECT response: {} bytes received",
                   callbacks_->connection(), length);
    return true;
  }
  const auto status = parser.parser().statusCode();
  if (status != Http::Code::OK) {
    failure_reason_ = absl::StrCat("proxy CONNECT failed: ", static_cast<uint64_t>(status));
    ENVOY_CONN_LOG(debug, "{}", callbacks_->connection(), failure_reason_);
    return false;
  }

  inbound.drain(bytes_processed);
  ENVOY_CONN_LOG(trace, "Successfully stripped {} bytes of CONNECT response",
                 callbacks_->connection(), bytes_processed);
  return onTunnelEstablished();
}

bool UpstreamHttp11TunnelSocket::onTunnelEstablished() {
  tunnel_established_ = true;
  // Start the origin socket. A plaintext socket raises the Connected event here; a TLS socket
  // raises it once its handshake through the tunnel completes.
  origin_socket_->onConnected();
  if (socketClosed()) {
    return true;
  }
  // Drive the origin socket so it can start its handshake through the tunnel.
  Buffer::OwnedImpl empty;
  if (origin_socket_->doWrite(empty, false).action_ == Network::PostIoAction::Close ||
      flushOutbound().action_ == Network::PostIoAction::Close) {
    return false;
  }
  // Flush any data that was held while the tunnel was being established.
  callbacks_->flushWriteBuffer();
  return true;
}

UpstreamHttp11ConnectSocketFactory::UpstreamHttp11ConnectSocketFactory(
    Network::UpstreamTransportSocketFactoryPtr transport_socket_factory,
    std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info,
    Network::UpstreamTransportSocketFactoryPtr proxy_socket_factory,
    Secret::ThreadLocalGenericSecretProviderPtr proxy_authorization,
    std::string static_proxy_authorization)
    : PassthroughFactory(std::move(transport_socket_factory)), proxy_info_(proxy_info),
      proxy_socket_factory_(std::move(proxy_socket_factory)),
      proxy_authorization_(std::move(proxy_authorization)),
      static_proxy_authorization_(std::move(static_proxy_authorization)) {
  ASSERT((proxy_authorization_ == nullptr && static_proxy_authorization_.empty()) ||
         proxy_socket_factory_ != nullptr);
  ASSERT(proxy_authorization_ == nullptr || static_proxy_authorization_.empty());
}

Network::TransportSocketPtr UpstreamHttp11ConnectSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    std::shared_ptr<const Upstream::HostDescription> host) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  if (proxy_socket_factory_ == nullptr) {
    // Legacy mode.
    return std::make_unique<UpstreamHttp11ConnectSocket>(std::move(inner_socket), options, host,
                                                         proxy_info_);
  }

  // Tunnel mode. Connections that are not routed through a proxy use the inner socket directly.
  const std::optional<std::string> target = UpstreamHttp11TunnelSocket::connectTarget(
      options, host.get(), proxy_info_.has_value(), inner_socket->ssl() != nullptr);
  if (!target.has_value()) {
    return inner_socket;
  }
  // The transport socket options describe the connection to the origin (SNI, SAN and ALPN
  // overrides), so they are not passed to the socket connecting to the proxy.
  auto proxy_socket = proxy_socket_factory_->createTransportSocket(nullptr, host);
  if (proxy_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<UpstreamHttp11TunnelSocket>(
      std::move(inner_socket), std::move(proxy_socket), target.value(),
      proxy_authorization_ != nullptr ? absl::string_view(proxy_authorization_->secret())
                                      : absl::string_view(static_proxy_authorization_));
}

void UpstreamHttp11ConnectSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  PassthroughFactory::hashKey(key, options);
  if (options && options->http11ProxyInfo().has_value()) {
    pushScalarToByteVector(
        StringUtil::CaseInsensitiveHash()(options->http11ProxyInfo()->proxy_address->asString()),
        key);
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(options->http11ProxyInfo()->hostname),
                           key);
  }
}

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

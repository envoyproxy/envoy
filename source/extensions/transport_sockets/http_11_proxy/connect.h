#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/http1/balsa_parser.h"
#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/http_11_proxy/tunnel_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

// If the transport socket options contain http11ProxyInfo and the transport is
// secure, this will prepend a CONNECT request to the outbound data and strip
// the CONNECT response from the inbound data.
class UpstreamHttp11ConnectSocket : public TransportSockets::PassthroughSocket,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  // Processes response_payload and returns true if it is a valid connect
  // response with status code 200.
  // @param response_payload the HTTP response bytes
  // @param header_complete will be set to true if the response payload contains complete headers.
  // @param bytes_processed will return how many bytes of response_payload were processed.
  static bool isValidConnectResponse(absl::string_view response_payload, bool& headers_complete,
                                     size_t& bytes_processed);

  // Helper method to create a properly formatted CONNECT request with Host header.
  // @param target the target hostname:port or IP:port to connect to.
  // @return a properly formatted CONNECT request string per RFC 9110 section 9.3.6.
  static std::string formatConnectRequest(absl::string_view target);

  UpstreamHttp11ConnectSocket(
      Network::TransportSocketPtr&& transport_socket,
      Network::TransportSocketOptionsConstSharedPtr options,
      std::shared_ptr<const Upstream::HostDescription> host,
      std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info = std::nullopt);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;

private:
  void generateHeader();
  Network::IoResult writeHeader();

  inline void
  handleProxyInfoConnect(const Network::TransportSocketOptions::Http11ProxyInfo& proxy_info);
  inline void handleHostMetadataConnect(std::shared_ptr<const Upstream::HostDescription> host);

  Network::TransportSocketOptionsConstSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_;
  bool need_to_strip_connect_response_{};
};

// Tunnel mode socket, used when the connection to the proxy is configured with its own transport
// socket and/or proxy credentials. The connection to the proxy is handled by proxy_socket_ (for
// example TLS), a CONNECT request is always sent through it, and once the proxy answers with a 200
// response, the origin socket (TLS or plaintext) runs inside the tunnel through a TunnelIoHandle.
class UpstreamHttp11TunnelSocket : public Network::TransportSocket,
                                   public Logger::Loggable<Logger::Id::connection> {
public:
  // Limit the CONNECT response headers to an arbitrary 2000 bytes, as the legacy socket does.
  static constexpr uint32_t MaxResponseHeaderSize = 2000;

  // Returns the CONNECT target (host:port) for a proxied connection, or nullopt if the connection
  // to this host is not proxied.
  // @param options the transport socket options for the connection.
  // @param host the upstream host.
  // @param has_default_proxy whether the factory has a default proxy address configured.
  // @param origin_is_tls whether the origin transport socket is secure. Used to select the default
  //        port when the target from the transport socket options has none.
  static std::optional<std::string>
  connectTarget(const Network::TransportSocketOptionsConstSharedPtr& options,
                const Upstream::HostDescription* host, bool has_default_proxy, bool origin_is_tls);

  // Builds the CONNECT request for the given target, adding a Proxy-Authorization header if
  // proxy_authorization is not empty.
  static std::string formatConnectRequest(absl::string_view target,
                                          absl::string_view proxy_authorization);

  UpstreamHttp11TunnelSocket(Network::TransportSocketPtr&& origin_socket,
                             Network::TransportSocketPtr&& proxy_socket, absl::string_view target,
                             absl::string_view proxy_authorization);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return origin_socket_->protocol(); }
  absl::string_view failureReason() const override;
  bool canFlushClose() override {
    return origin_socket_->canFlushClose() && proxy_socket_->canFlushClose();
  }
  Api::SysCallIntResult connect(Network::ConnectionSocket& socket) override {
    return proxy_socket_->connect(socket);
  }
  void closeSocket(Network::ConnectionEvent event, bool abort_reset) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override { proxy_socket_->onConnected(); }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return origin_socket_->ssl(); }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override {
    proxy_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec, rtt);
  }

  bool tunnelEstablished() const { return tunnel_established_; }

private:
  // Callbacks handed to the socket that talks to the proxy. It operates on the real IoHandle, but
  // its Connected event is not propagated: the connection is only connected once the origin socket
  // is connected through the tunnel.
  class ProxyCallbacks : public Network::TransportSocketCallbacks {
  public:
    explicit ProxyCallbacks(UpstreamHttp11TunnelSocket& parent) : parent_(parent) {}
    Network::IoHandle& ioHandle() override { return parent_.callbacks_->ioHandle(); }
    const Network::IoHandle& ioHandle() const override {
      return static_cast<const Network::TransportSocketCallbacks*>(parent_.callbacks_)->ioHandle();
    }
    Network::Connection& connection() override { return parent_.callbacks_->connection(); }
    bool shouldDrainReadBuffer() override;
    void setTransportSocketIsReadable() override {
      parent_.callbacks_->setTransportSocketIsReadable();
    }
    void raiseEvent(Network::ConnectionEvent event) override;
    void flushWriteBuffer() override { parent_.callbacks_->flushWriteBuffer(); }

  private:
    UpstreamHttp11TunnelSocket& parent_;
  };

  // Callbacks handed to the origin socket. All I/O goes through the TunnelIoHandle.
  class TunnelCallbacks : public Network::TransportSocketCallbacks {
  public:
    explicit TunnelCallbacks(UpstreamHttp11TunnelSocket& parent) : parent_(parent) {}
    Network::IoHandle& ioHandle() override { return *parent_.tunnel_io_handle_; }
    const Network::IoHandle& ioHandle() const override { return *parent_.tunnel_io_handle_; }
    Network::Connection& connection() override { return parent_.callbacks_->connection(); }
    bool shouldDrainReadBuffer() override { return parent_.callbacks_->shouldDrainReadBuffer(); }
    void setTransportSocketIsReadable() override {
      parent_.callbacks_->setTransportSocketIsReadable();
    }
    void raiseEvent(Network::ConnectionEvent event) override {
      parent_.callbacks_->raiseEvent(event);
    }
    void flushWriteBuffer() override { parent_.callbacks_->flushWriteBuffer(); }

  private:
    UpstreamHttp11TunnelSocket& parent_;
  };

  // Processes the CONNECT response in the inbound buffer. Returns false if the connection must be
  // closed.
  bool processConnectResponse();
  // Called once the proxy accepted the CONNECT request. Returns false if the connection must be
  // closed.
  bool onTunnelEstablished();
  // Sends the data written by the origin socket to the proxy socket.
  Network::IoResult flushOutbound();
  bool socketClosed() const { return !callbacks_->ioHandle().isOpen(); }

  Network::TransportSocketPtr origin_socket_;
  Network::TransportSocketPtr proxy_socket_;
  Network::TransportSocketCallbacks* callbacks_{};
  std::unique_ptr<TunnelIoHandle> tunnel_io_handle_;
  ProxyCallbacks proxy_callbacks_{*this};
  TunnelCallbacks tunnel_callbacks_{*this};
  Buffer::OwnedImpl connect_request_;
  std::string failure_reason_;
  bool tunnel_established_{false};
  bool proxy_end_stream_{false};
};

class UpstreamHttp11ConnectSocketFactory : public PassthroughFactory {
public:
  // @param transport_socket_factory the factory of the origin (inner) transport socket.
  // @param proxy_info the default proxy information, if any.
  // @param proxy_socket_factory if set, enables tunnel mode and is used to create the transport
  //        socket for the connection to the proxy.
  // @param proxy_authorization if set, provides the value of the Proxy-Authorization header sent
  //        in the CONNECT request. Requires proxy_socket_factory to be set.
  // @param static_proxy_authorization if not empty, the value of the Proxy-Authorization header
  //        sent in the CONNECT request (e.g. built from Basic credentials). Mutually exclusive with
  //        proxy_authorization. Requires proxy_socket_factory to be set.
  UpstreamHttp11ConnectSocketFactory(
      Network::UpstreamTransportSocketFactoryPtr transport_socket_factory,
      std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info = std::nullopt,
      Network::UpstreamTransportSocketFactoryPtr proxy_socket_factory = nullptr,
      Secret::ThreadLocalGenericSecretProviderPtr proxy_authorization = nullptr,
      std::string static_proxy_authorization = "");

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        std::shared_ptr<const Upstream::HostDescription> host) const override;
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;

  OptRef<const Network::TransportSocketOptions::Http11ProxyInfo>
  defaultHttp11ProxyInfo() const override {
    if (!proxy_info_.has_value()) {
      return {};
    }
    return {proxy_info_.value()};
  }
  bool tunnelsHttp11ProxyTraffic() const override { return proxy_socket_factory_ != nullptr; }

private:
  const std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info_;
  const Network::UpstreamTransportSocketFactoryPtr proxy_socket_factory_;
  const Secret::ThreadLocalGenericSecretProviderPtr proxy_authorization_;
  const std::string static_proxy_authorization_;
};

// This is a utility class for isValidConnectResponse. It is only exposed for
// coverage testing purposes. See isValidConnectResponse for intended use.
class SelfContainedParser : public Http::Http1::ParserCallbacks {
public:
  SelfContainedParser()
      : parser_(Http::Http1::MessageType::Response, this, 2000, /* enable_trailers = */ false,
                /* allow_custom_methods = */ false) {}
  Http::Http1::CallbackResult onMessageBegin() override {
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onUrl(const char*, size_t) override {
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onStatus(const char*, size_t) override {
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeaderField(const char*, size_t) override {
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeaderValue(const char*, size_t) override {
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeadersComplete() override {
    headers_complete_ = true;
    parser_.pause();
    return Http::Http1::CallbackResult::Success;
  }
  void bufferBody(const char*, size_t) override {}
  Http::Http1::CallbackResult onMessageComplete() override {
    return Http::Http1::CallbackResult::Success;
  }
  void onChunkHeader(bool) override {}

  bool headersComplete() const { return headers_complete_; }
  Http::Http1::BalsaParser& parser() { return parser_; }

private:
  bool headers_complete_ = false;
  Http::Http1::BalsaParser parser_;
};

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/http1/balsa_parser.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

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
  static bool isValidConnectResponse(Buffer::Instance& buffer);

  UpstreamHttp11ConnectSocket(Network::TransportSocketPtr&& transport_socket,
                              Network::TransportSocketOptionsConstSharedPtr options);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;

private:
  void generateHeader();
  Network::IoResult writeHeader();

  Network::TransportSocketOptionsConstSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_{};
  bool need_to_strip_connect_response_{};
};

class UpstreamHttp11ConnectSocketFactory : public PassthroughFactory {
public:
  UpstreamHttp11ConnectSocketFactory(
      Network::UpstreamTransportSocketFactoryPtr transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        std::shared_ptr<const Upstream::HostDescription> host) const override;
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;
};

// This is a utility class for isValidConnectResponse. It is only exposed for
// coverage testing purposes. See isValidConnectResponse for intended use.
class SelfContainedParser : public Http::Http1::ParserCallbacks {
public:
  SelfContainedParser()
      : parser_(Http::Http1::MessageType::Response, this, 2000, /* enable_trailers = */ false) {}
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

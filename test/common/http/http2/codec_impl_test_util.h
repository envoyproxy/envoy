#include "envoy/http/codec.h"

#include "common/http/http2/codec_impl.h"

namespace Envoy {
namespace Http {
namespace Http2 {

class TestServerConnectionImpl : public ServerConnectionImpl {
public:
  TestServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings,
                           uint32_t max_request_headers_kb)
      : ServerConnectionImpl(connection, callbacks, scope, http2_settings, max_request_headers_kb) {
  }
  nghttp2_session* session() { return session_; }
  using ServerConnectionImpl::getStream;
};

class TestClientConnectionImpl : public ClientConnectionImpl {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings,
                           uint32_t max_request_headers_kb)
      : ClientConnectionImpl(connection, callbacks, scope, http2_settings, max_request_headers_kb) {
  }
  nghttp2_session* session() { return session_; }
  using ClientConnectionImpl::getStream;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy

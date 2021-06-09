#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

#ifdef ENVOY_ENABLE_QUIC

class FilterIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void initialize() override {
    const std::string filter = R"EOF(
name: alternate_protocols_cache
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig
  alternate_protocols_cache_options:
    name: default_alternate_protocols_cache
)EOF";
    config_helper_.addFilter(filter);

    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(/*use_alpn=*/true, /*http3=*/true,
                                        /*use_alternate_protocols_cache=*/true);

    HttpProtocolIntegrationTest::initialize();
  }

  void createUpstreams() override {
    // The integration test infrastructure does not know how to create mixed protocol upstreams.
    // It will have created an HTTP/3 upstream, so the test needs to create an additional
    // HTTP/2 upstream manually.
    ASSERT_EQ(upstreamProtocol(), Http::CodecType::HTTP3);
    ASSERT_EQ(fake_upstreams_count_, 1);
    ASSERT_FALSE(autonomous_upstream_);

    auto config = configWithType(Http::CodecType::HTTP2);
    Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
    addFakeUpstream(std::move(factory), Http::CodecType::HTTP2);
  }
};

TEST_P(FilterIntegrationTest, AltSvc) {
  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  int port = fake_upstreams_[0]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3-29=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2. The response includes an Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);

  // Second request should go out over HTTP/3 because of the Alt-Svc information.
  auto response2 = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                 response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);
}

INSTANTIATE_TEST_SUITE_P(Protocols, FilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

#endif

} // namespace
} // namespace Envoy

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TcpTunnelingIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  TcpTunnelingIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override {
    enable_half_close_ = true;
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          envoy::config::filter::network::tcp_proxy::v2::TcpProxy proxy_config;
          proxy_config.set_stat_prefix("tcp_stats");
          proxy_config.set_cluster("cluster_0");
          proxy_config.mutable_tunneling_config()->set_hostname("host.com");

          auto* listener = bootstrap.mutable_static_resources()->add_listeners();
          listener->set_name("tcp_proxy");
          auto* socket_address = listener->mutable_address()->mutable_socket_address();
          socket_address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
          socket_address->set_port_value(0);

          auto* filter_chain = listener->add_filter_chains();
          auto* filter = filter_chain->add_filters();
          filter->mutable_typed_config()->PackFrom(proxy_config);
          filter->set_name("envoy.filters.network.tcp_proxy");
        });
  }
};

TEST_P(TcpTunnelingIntegrationTest, Basic) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send some data from downstream to upstream, and make sure it goes through.
  tcp_client->write("hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream.
  upstream_request_->encodeData(12, false);
  tcp_client->waitForData(12);

  // Now send more data and close the TCP client. This should be treated as half close, so the data
  // should go through.
  tcp_client->write("hello", false);
  tcp_client->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // If the upstream now sends 'end stream' the connection is fully closed.
  upstream_request_->encodeData(0, true);
}

TEST_P(TcpTunnelingIntegrationTest, InvalidResponseHeaders) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send invalid response headers, and verify that the client disconnects and
  // upstream gets a stream reset.
  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(upstream_request_->waitForReset());

  // The connection should be fully closed, but the client has no way of knowing
  // that. Ensure the FIN is read and clean up state.
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(TcpTunnelingIntegrationTest, CloseUpstreamFirst) {
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  tcp_client->write("hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData(12, true);
  tcp_client->waitForData(12);
  tcp_client->waitForHalfClose();

  // Attempt to send data upstream.
  // should go through.
  tcp_client->write("hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  tcp_client->write("hello", true);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
}

TEST_P(TcpTunnelingIntegrationTest, ResetStreamTest) {
  enable_half_close_ = false;
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Reset the stream.
  upstream_request_->encodeResetStream();
  tcp_client->waitForDisconnect(true);
}

TEST_P(TcpTunnelingIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
  enable_half_close_ = false;
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(1);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>(*config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  std::string data(1024 * 16, 'a');
  tcp_client->write(data);
  upstream_request_->encodeData(data, false);

  tcp_client->waitForDisconnect(true);
  ASSERT_TRUE(upstream_request_->waitForReset());
}

// Test that a downstream flush works correctly (all data is flushed)
TEST_P(TcpTunnelingIntegrationTest, TcpProxyDownstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 4, size / 4);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  tcp_client->readDisable(true);
  tcp_client->write("", true);

  // This ensures that readDisable(true) has been run on its thread
  // before tcp_client starts writing.
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeData(data, true);

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  tcp_client->readDisable(false);
  tcp_client->waitForData(data);
  tcp_client->waitForHalfClose();
}

// Test that an upstream flush works correctly (all data is flushed)
TEST_P(TcpTunnelingIntegrationTest, TcpProxyUpstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->readDisable(true);
  upstream_request_->encodeData("", true);

  // This ensures that fake_upstream_connection->readDisable has been run on its thread
  // before tcp_client starts writing.
  tcp_client->waitForHalfClose();

  tcp_client->write(data, true);

  // Note that upstream_flush_active will *not* be incremented for the HTTP
  // tunneling case. The data is already written to the stream, so no drainer
  // is necessary.
  upstream_request_->readDisable(false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, size));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  tcp_client->waitForHalfClose();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpTunnelingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace
} // namespace Envoy

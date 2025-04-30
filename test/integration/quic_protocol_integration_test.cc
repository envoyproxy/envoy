#include "source/common/network/socket_option_impl.h"

#include "test/integration/protocol_integration_test.h"

namespace Envoy {

// Test that the quiche code can handle packets getting batched together, i.e.
// that it will re-register to read even without incoming packets.
// TODO(alyssawilk) debug https://github.com/envoyproxy/envoy/issues/36265
TEST_P(DownstreamProtocolIntegrationTest, DISABLED_BatchedPackets) {
  if (downstreamProtocol() != Http::CodecType::HTTP3) {
    return; // Testing H3 client talking to H3 upstream only.
  }
  setUpstreamProtocol(Http::CodecType::HTTP3);
  initialize();

  // Set up the transport factory so the codec client will have credentials to
  // talk to the upstream.
  quic_transport_socket_factory_ = IntegrationUtil::createQuicUpstreamTransportSocketFactory(
      *api_, stats_store_, context_manager_, thread_local_, san_to_match_,
      true /*connect to upstream*/);

  // Connect directly to the upstream.
  int upstream_port = fake_upstreams_[0]->localAddress()->ip()->port();
  // Make sure the client receive buffer can handle all the packets without loss.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024 * 100));

  codec_client_ = makeHttpConnection(makeClientConnectionWithOptions(upstream_port, options));

  // Send a request and a response that can not be handled in one read.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  // Send more than the 32 packets. Generally all packets will be read in one pass.
  // All packet are sent here where the client is not looping, so can not read. The upstream is
  // then deadlocked, guaranteeing all packets are sent to the kernel before the client performs
  // any reads. Manual testing confirms they're consistently read at once. There are no guarantees
  // of this, but given the test uses loopback sockets it's likely to continue to be the case.
  upstream_request_->encodeData(1024 * 35, true);

  // Now deadlock the upstream so it can not do anything - no acks, no
  // retransmissions.
  absl::Notification unblock_upstream;
  absl::Notification upstream_blocked;
  fake_upstreams_[0]->runOnDispatcherThread([&] {
    upstream_blocked.Notify();
    unblock_upstream.WaitForNotification();
  });
  upstream_blocked.WaitForNotification();

  // Make sure all the packets are read by the client.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Unblock the upstream.
  unblock_upstream.Notify();
  ASSERT_TRUE(fake_upstream_connection_->close());
}

// When downstream protocol is HTTP3 and upstream protocol is HTTP1, an OPTIONS
// request with no body will not append transfer-encoding chunked.
TEST_P(DownstreamProtocolIntegrationTest, OptionsWithNoBodyNotChunked) {
  // This test is only relevant for H3 downstream to H1 upstream.
  if (downstreamProtocol() != Http::CodecType::HTTP3 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"},
      {":path", "/foo"},
      {":scheme", "http"},
      {":authority", "host"},
      {"access-control-request-method", "GET"},
      {"origin", "test-origin"},
  };
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().TransferEncoding(), nullptr);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  EXPECT_EQ(response->headers().TransferEncoding(), nullptr);
}

// These will run with HTTP/3 downstream, and Http upstream.
INSTANTIATE_TEST_SUITE_P(DownstreamProtocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// These will run with HTTP/3 downstream, and Http and HTTP/2 upstream.
INSTANTIATE_TEST_SUITE_P(DownstreamProtocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// These will run with HTTP/1 and HTTP/2 downstream, and HTTP/3 upstream.
INSTANTIATE_TEST_SUITE_P(UpstreamProtocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

INSTANTIATE_TEST_SUITE_P(UpstreamProtocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy

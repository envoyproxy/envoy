#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/upstreams/http/udp/v3/udp_connection_pool.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Terminating CONNECT and sending raw TCP upstream.
class ConnectUdpTerminationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConnectUdpTerminationIntegrationTest() {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          ConfigHelper::setConnectUdpConfig(hcm, true,
                                            downstream_protocol_ == Http::CodecType::HTTP3);
        });
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    HttpIntegrationTest::initialize();
  }

  void setUpConnection() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(connect_udp_headers_);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    response_->waitForHeaders();
  }

  void sendBidirectionalData(const char* downstream_send_data = "hello",
                             const char* upstream_received_data = "hello",
                             const char* upstream_send_data = "there!",
                             const char* downstream_received_data = "there!") {
    // Send some data upstream.
    codec_client_->sendData(*request_encoder_, downstream_send_data, false);
    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(upstream_received_data, request_datagram.buffer_->toString());

    // Send some data downstream.
    fake_upstreams_[0]->sendUdpDatagram(upstream_send_data, request_datagram.addresses_.peer_);
    response_->waitForBodyData(strlen(downstream_received_data));
    EXPECT_EQ(downstream_received_data, response_->body());
  }

  Http::TestRequestHeaderMapImpl connect_udp_headers_{
      {":method", "CONNECT"},
      {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
      {":protocol", "connect-udp"},
      {":scheme", "https"},
      {":authority", "example.org"}};

  IntegrationStreamDecoderPtr response_;
};

TEST_P(ConnectUdpTerminationIntegrationTest, Basic) {
  initialize();
  connect_udp_headers_.clear();
  // The client H/2 codec expects the request header map to be in the form of H/1
  // upgrade to issue an extended CONNECT request
  connect_udp_headers_.copyFrom(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
                                     {"upgrade", "connect-udp"},
                                     {"connection", "upgrade"},
                                     {":scheme", "https"},
                                     {":authority", "example.org"}});
  setUpConnection();
  sendBidirectionalData();
}

INSTANTIATE_TEST_SUITE_P(HttpVersions, ConnectUdpTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy

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

// Terminates CONNECT-UDP and sends raw UDP datagrams upstream.
class ConnectUdpTerminationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConnectUdpTerminationIntegrationTest() {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          if (enable_timeout_) {
            hcm.mutable_stream_idle_timeout()->set_seconds(0);
            hcm.mutable_stream_idle_timeout()->set_nanos(200 * 1000 * 1000);
          }
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

  void sendBidirectionalData(std::string downstream_send_data = "hello",
                             std::string upstream_received_data = "hello",
                             std::string upstream_send_data = "there!",
                             std::string downstream_received_data = "there!") {
    // Send some data upstream.
    codec_client_->sendData(*request_encoder_, downstream_send_data, false);
    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(upstream_received_data, request_datagram.buffer_->toString());

    // Send some data downstream.
    fake_upstreams_[0]->sendUdpDatagram(upstream_send_data, request_datagram.addresses_.peer_);
    response_->waitForBodyData(downstream_received_data.length());
    EXPECT_EQ(downstream_received_data, response_->body());
  }

  // The Envoy HTTP/2 and HTTP/3 clients expect the request header map to be in the form of HTTP/1
  // upgrade to issue an extended CONNECT request.
  Http::TestRequestHeaderMapImpl connect_udp_headers_{
      {":method", "GET"},         {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
      {"upgrade", "connect-udp"}, {"connection", "upgrade"},
      {":scheme", "https"},       {":authority", "example.org"},
      {"capsule-protocol", "?1"}};

  IntegrationStreamDecoderPtr response_;
  bool enable_timeout_{};
};

TEST_P(ConnectUdpTerminationIntegrationTest, Basic) {
  initialize();
  setUpConnection();
  std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  std::string received_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );

  sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                        absl::HexStringToBytes("a1a2a3a4a5a6a7a8"), received_capsule_fragment);
}

TEST_P(ConnectUdpTerminationIntegrationTest, BasicWithoutCapsuleProtocolHeader) {
  initialize();
  connect_udp_headers_.remove(Envoy::Http::Headers::get().CapsuleProtocol);
  setUpConnection();
  std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  std::string received_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );

  sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                        absl::HexStringToBytes("a1a2a3a4a5a6a7a8"), received_capsule_fragment);
}



TEST_P(ConnectUdpTerminationIntegrationTest, DownstreamClose) {
  initialize();

  setUpConnection();

  std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  std::string received_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );

  sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                        absl::HexStringToBytes("a1a2a3a4a5a6a7a8"), received_capsule_fragment);

  // Tear down by closing the client connection.
  codec_client_->close();
}

TEST_P(ConnectUdpTerminationIntegrationTest, DownstreamReset) {
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    // Resetting an individual stream requires HTTP/2 or later.
    return;
  }
  initialize();

  setUpConnection();
    std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  std::string received_capsule_fragment =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );

  sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                        absl::HexStringToBytes("a1a2a3a4a5a6a7a8"), received_capsule_fragment);

  // Tear down by resetting the client stream.
  codec_client_->sendReset(*request_encoder_);
}



TEST_P(ConnectUdpTerminationIntegrationTest, StreamIdleTimeout) {
  enable_timeout_ = true;
  initialize();
  setUpConnection();

  // Wait for the timeout to close the connection.
  ASSERT_TRUE(response_->waitForReset());
}



TEST_P(ConnectUdpTerminationIntegrationTest, MaxStreamDuration) {
  setUpstreamProtocol(upstreamProtocol());
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_common_http_protocol_options()
        ->mutable_max_stream_duration()
        ->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();
  setUpConnection();
  std::string sent_capsule_fragment =
  absl::HexStringToBytes("00"               // DATAGRAM capsule type
                          "08"               // capsule length
                          "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
  );
  std::string received_capsule_fragment =
  absl::HexStringToBytes("00"               // DATAGRAM capsule type
                          "08"               // capsule length
                          "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
  );

  sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                        absl::HexStringToBytes("a1a2a3a4a5a6a7a8"), received_capsule_fragment);

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response_->waitForReset());
    codec_client_->close();
  }
}

TEST_P(ConnectUdpTerminationIntegrationTest, PathWithInvalidUriTemplate) {
  initialize();
  connect_udp_headers_.setPath("/masque/udp/foo.lyft.com/80/");
  setUpConnection();
  EXPECT_EQ("400", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, DropUnknownCapsules) {
  initialize();
  setUpConnection();
  Network::UdpRecvData request_datagram;
  std::string unknown_capsule_fragment =
      absl::HexStringToBytes("01"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "00a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  codec_client_->sendData(*request_encoder_, unknown_capsule_fragment, false);
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::milliseconds(100)));

  std::string unknown_context_id =
      absl::HexStringToBytes("00"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "01a1a2a3a4a5a6a7" // UDP Proxying HTTP Datagram payload
      );
  codec_client_->sendData(*request_encoder_, unknown_context_id, false);
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::milliseconds(100)));
}

INSTANTIATE_TEST_SUITE_P(HttpVersions, ConnectUdpTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha/golang.pb.h"

namespace Envoy {
namespace {

class GolangBridgeIntegrationTest : public HttpProtocolIntegrationTest {
public:
  GolangBridgeIntegrationTest() { enableHalfClose(true); }

  std::string genSoPath() {
    return TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/golang/upstreams/http/tcp/test/test_data/bridges.so");
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          hcm.mutable_stream_idle_timeout()->set_seconds(5);

          auto* route_config = hcm.mutable_route_config();
          ASSERT_EQ(1, route_config->virtual_hosts_size());
          route_config->mutable_virtual_hosts(0)->clear_domains();
          route_config->mutable_virtual_hosts(0)->add_domains("*");
        });
    HttpIntegrationTest::initialize();
  }

  void initializeConfig(const std::string& bridge_name, const std::string& plugin_config = "") {
    config_helper_.addConfigModifier(
        [this, bridge_name, plugin_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_upstream_config()->set_name("envoy.upstreams.http.tcp.golang");
          cluster->mutable_upstream_config()->mutable_typed_config()->set_type_url(
              "type.googleapis.com/envoy.extensions.upstreams.http.tcp.golang.v3alpha.Config");

          envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config proto_config;
          proto_config.set_library_id(bridge_name);
          proto_config.set_library_path(genSoPath());
          proto_config.set_plugin_name(bridge_name);

          cluster->mutable_upstream_config()->mutable_typed_config()->PackFrom(proto_config);
        });

    initialize();
  }

  void setUpConnection(bool header_only_request = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers_, header_only_request);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_));
  }

  // msg for golang bridge test
  const std::string data_to_upstream_{"data-to-upstream-value"};
  const std::string panic_msg_{"error happened in golang http-tcp bridge\r\n"};

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "POST"},
                                                  {":authority", "golang.bridge.com:80"},
                                                  {":path", "/"},
                                                  {"data-to-upstream", data_to_upstream_}};

  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;

  // golang bridge names
  const std::string STREAMING{"streaming"};
  const std::string BUFFERED{"buffered"};
  const std::string ADD_DATA{"add_data"};
  const std::string LOCAL_REPLY{"local_reply"};
  const std::string HEADER_OP{"header_op"};
  const std::string PROPERTY{"property"};
  const std::string PANIC{"panic"};
  const std::string SELF_HALF_CLOSE{"self_half_close"};
};

// TODO(duxin40): add test for HTTP2.
INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, GolangBridgeIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(GolangBridgeIntegrationTest, Streaming) {

  initializeConfig(STREAMING);
  setUpConnection();

  codec_client_->sendData(*request_encoder_, "http_data-1", false);

  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("streaming-http-to-tcp:http_data-1")));
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-1"));

  codec_client_->sendData(*request_encoder_, "http_data-2", false);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("streaming-http-to-tcp:http_data-2")));
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-2-end", false));

  std::string expected_resp_body = "streaming-tcp-to-http:tcp_resp-1tcp_resp-2-end";
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, Buffered) {

  initializeConfig(BUFFERED);
  setUpConnection();

  codec_client_->sendData(*request_encoder_, "http_data-1", false);
  codec_client_->sendData(*request_encoder_, "http_data-2", true);

  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("buffered-http-to-tcp:http_data-1http_data-2")));

  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-1", false));
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-2-end", false));

  std::string expected_resp_body = "buffered-tcp-to-http:tcp_resp-1tcp_resp-2-end";
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, ADD_DATA_Encode_Headers) {

  initializeConfig(ADD_DATA);
  setUpConnection(true);

  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch(data_to_upstream_.c_str())));

  cleanupUpstreamAndDownstream();
}

TEST_P(GolangBridgeIntegrationTest, LOCAL_REPLY_Encode_Headers) {

  initializeConfig(LOCAL_REPLY);
  setUpConnection(true);

  std::string expected_resp_body = "local_reply-http-to-tcp-encode_headers";
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, LOCAL_REPLY_Encode_Data) {

  initializeConfig(LOCAL_REPLY);
  setUpConnection();

  codec_client_->sendData(*request_encoder_, "please-local-reply", false);

  std::string expected_resp_body = "local_reply-http-to-tcp-encode_data";
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, HEADER_OP) {

  initializeConfig(HEADER_OP);
  setUpConnection(true);

  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch(
      "header_op-http-to-tcp-encode_headers-golang.bridge.com:80")));

  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-1", false));

  response_->waitForHeaders();
  EXPECT_EQ("202", response_->headers().getStatusValue());
}

TEST_P(GolangBridgeIntegrationTest, PROPERTY) {

  initializeConfig(PROPERTY);
  setUpConnection(true);

  std::string expected_resp_body = "property-http-to-tcp-encode_headers-route_config_0-cluster_0";
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, PANIC_Encode_Headers) {

  initializeConfig(PANIC);
  setUpConnection(true);

  std::string expected_resp_body = panic_msg_;
  response_->waitForBodyData(panic_msg_.size());
  EXPECT_EQ(panic_msg_, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, PANIC_Encode_Data) {

  initializeConfig(PANIC);
  setUpConnection();

  codec_client_->sendData(*request_encoder_, "http_data-1", true);

  std::string expected_resp_body = panic_msg_;
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, PANIC_On_Upstream_Data) {

  initializeConfig(PANIC);
  setUpConnection();

  codec_client_->sendData(*request_encoder_, "http_data-1", false);
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_resp-1", false));

  std::string expected_resp_body = panic_msg_;
  response_->waitForBodyData(expected_resp_body.size());
  EXPECT_EQ(expected_resp_body, response_->body());
}

TEST_P(GolangBridgeIntegrationTest, SELF_HALF_CLOSE) {

  initializeConfig(SELF_HALF_CLOSE);
  setUpConnection();

  // Send an end stream. This should result in half close upstream.
  codec_client_->sendData(*request_encoder_, "", true);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

  // Now send a FIN from upstream. This should result in clean shutdown downstream.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  ASSERT_TRUE(response_->waitForEndStream());
}

} // namespace
} // namespace Envoy

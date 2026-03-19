#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace {

class DynamicModuleBridgeIntegrationTest : public HttpProtocolIntegrationTest {
public:
  DynamicModuleBridgeIntegrationTest() { enableHalfClose(true); }

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

  void initializeWithBridgeConfig(const std::string& bridge_mode) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    config_helper_.addConfigModifier(
        [bridge_mode](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_upstream_config()->set_name("envoy.upstreams.http.dynamic_modules");
          cluster->mutable_upstream_config()->mutable_typed_config()->set_type_url(
              "type.googleapis.com/"
              "envoy.extensions.upstreams.http.dynamic_modules.v3.Config");

          envoy::extensions::upstreams::http::dynamic_modules::v3::Config proto_config;
          auto* dm_config = proto_config.mutable_dynamic_module_config();
          dm_config->set_name("upstream_http_tcp_bridge");
          dm_config->set_do_not_close(true);

          proto_config.set_bridge_name("test_bridge");

          // Pass bridge mode as config.
          Protobuf::StringValue config_value;
          config_value.set_value(bridge_mode);
          proto_config.mutable_bridge_config()->PackFrom(config_value);

          cluster->mutable_upstream_config()->mutable_typed_config()->PackFrom(proto_config);
        });

    initialize();
  }

  void setupConnection(bool header_only = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers_, header_only);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_));
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":authority", "test.host.com:80"}, {":path", "/"}};

  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, DynamicModuleBridgeIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(DynamicModuleBridgeIntegrationTest, StreamingMode) {
  initializeWithBridgeConfig("streaming");
  setupConnection();

  // Send first data chunk.
  codec_client_->sendData(*request_encoder_, "chunk1", false);

  // The streaming bridge prepends "METHOD=POST " during encode_headers.
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST ")));

  // Send upstream TCP response.
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_response_data"));

  // Verify response headers are received.
  response_->waitForHeaders();
  EXPECT_EQ("200", response_->headers().getStatusValue());

  // Send end of stream.
  codec_client_->sendData(*request_encoder_, "chunk2", true);
  ASSERT_TRUE(
      fake_raw_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("chunk2")));

  // Close upstream connection to end the response.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());

  // Wait for response completion.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_EQ("tcp_response_data", response_->body());
}

TEST_P(DynamicModuleBridgeIntegrationTest, LocalReplyMode) {
  initializeWithBridgeConfig("local_reply");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("access denied", response->body());
}

} // namespace
} // namespace Envoy

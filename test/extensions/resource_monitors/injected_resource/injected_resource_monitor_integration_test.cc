#include <openssl/x509_vfy.h>

#include <cstddef>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {

void updateResource(AtomicFileUpdater& updater, double pressure) {
  updater.update(absl::StrCat(pressure));
}

// A test that sets up its own client connection with customized quic version and connection ID.
class OverloadIntegrationTest : public HttpIntegrationTest {
public:
  OverloadIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam().first),
        injected_resource_filename_1_(TestEnvironment::temporaryPath("injected_resource_1")),
        injected_resource_filename_2_(TestEnvironment::temporaryPath("injected_resource_2")),
        file_updater_1_(injected_resource_filename_1_),
        file_updater_2_(injected_resource_filename_2_) {}

  ~OverloadIntegrationTest() override {
    cleanupUpstreamAndDownstream();
    // Release the client before destroying |conn_helper_|. No such need once |conn_helper_| is
    // moved into a client connection factory in the base test class.
    codec_client_.reset();
  }

  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options) override {
    IntegrationCodecClientPtr codec =
        HttpIntegrationTest::makeRawHttpConnection(std::move(conn), http2_options);
    if (!codec->disconnected()) {
      codec->setCodecClientCallbacks(client_codec_callback_);
      EXPECT_EQ(transport_socket_factory_->clientContextConfig().serverNameIndication(),
                codec->connection()->requestedServerName());
    }
    return codec;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      const std::string overload_config =
          fmt::format(R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.injected_resource_1"
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.resource_monitors.injected_resource.v3.InjectedResourceConfig
              filename: "{}"
          - name: "envoy.resource_monitors.injected_resource_2"
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.resource_monitors.injected_resource.v3.InjectedResourceConfig
              filename: "{}"
        actions:
          - name: "envoy.overload_actions.stop_accepting_requests"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_1"
                threshold:
                  value: 0.95
          - name: "envoy.overload_actions.stop_accepting_connections"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_1"
                threshold:
                  value: 0.9
          - name: "envoy.overload_actions.disable_http_keepalive"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_2"
                threshold:
                  value: 0.8
      )EOF",
                      injected_resource_filename_1_, injected_resource_filename_2_);
      *bootstrap.mutable_overload_manager() =
          TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(overload_config);
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_drain_timeout()->clear_seconds();
          hcm.mutable_drain_timeout()->set_nanos(500 * 1000 * 1000);
          EXPECT_EQ(hcm.codec_type(), envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager::HTTP3);
        });

    updateResource(file_updater_1_, 0);
    updateResource(file_updater_2_, 0);
    HttpIntegrationTest::initialize();
    // registerTestServerPorts({"http"});
  }

protected:
  CodecClientCallbacksForTest client_codec_callback_;
  Network::Address::InstanceConstSharedPtr server_addr_;
  const std::string injected_resource_filename_1_;
  const std::string injected_resource_filename_2_;
  AtomicFileUpdater file_updater_1_;
  AtomicFileUpdater file_updater_2_;
};

INSTANTIATE_TEST_SUITE_P(OverloadIntegrationTests, OverloadIntegrationTest,
                         testing::ValuesIn(generateTestParam()), testParamsToString);

TEST_P(OverloadIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initialize();

  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(file_updater_1_, 0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  // Reduce load a little to allow the connection to be accepted connection.
  updateResource(file_updater_1_, 0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  // Send response headers, but hold response body for now.
  upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);

  updateResource(file_updater_1_, 0.95);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);
  // Existing request should be able to finish.
  upstream_request_->encodeData(10, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // New request should be rejected.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response2->body());
  codec_client_->close();

  EXPECT_TRUE(makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt)
                  ->disconnected());
}

TEST_P(OverloadIntegrationTest, NoNewStreamsWhenOverloaded) {
  initialize();
  updateResource(file_updater_1_, 0.7);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Send a complete request and start a second.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);

  // Enable the disable-keepalive overload action. This should send a shutdown notice before
  // encoding the headers.
  updateResource(file_updater_2_, 0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 1);

  upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);
  upstream_request_->encodeData(10, true);

  response2->waitForHeaders();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(codec_client_->sawGoAway());
  codec_client_->close();
}

} // namespace Envoy

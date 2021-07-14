#include <openssl/x509_vfy.h>

#include <cstddef>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#if defined(ENVOY_CONFIG_COVERAGE)
#define DISABLE_UNDER_COVERAGE return
#else
#define DISABLE_UNDER_COVERAGE                                                                     \
  do {                                                                                             \
  } while (0)
#endif

namespace Envoy {

void updateResource(AtomicFileUpdater& updater, double pressure) {
  updater.update(absl::StrCat(pressure));
}

class OverloadIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  OverloadIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()),
        injected_resource_filename_1_(TestEnvironment::temporaryPath("injected_resource_1")),
        injected_resource_filename_2_(TestEnvironment::temporaryPath("injected_resource_2")),
        file_updater_1_(injected_resource_filename_1_),
        file_updater_2_(injected_resource_filename_2_) {}

  ~OverloadIntegrationTest() override {
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
  }

  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options) override {
    IntegrationCodecClientPtr codec =
        HttpIntegrationTest::makeRawHttpConnection(std::move(conn), http2_options);
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

    updateResource(file_updater_1_, 0);
    updateResource(file_updater_2_, 0);
    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

protected:
  Network::Address::InstanceConstSharedPtr server_addr_;
  const std::string injected_resource_filename_1_;
  const std::string injected_resource_filename_2_;
  AtomicFileUpdater file_updater_1_;
  AtomicFileUpdater file_updater_2_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OverloadIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(OverloadIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initialize();
  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(file_updater_1_, 0.95);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  IntegrationStreamDecoderPtr response;
  // For HTTP/2 and below, excess connection won't be accepted, but will hang out
  // in a pending state and resume below.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                         std::chrono::milliseconds(1000)));

  // Reduce load a little to allow the connection to be accepted.
  updateResource(file_updater_1_, 0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("202", response->headers().getStatusValue());
  codec_client_->close();
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

  codec_client_->close();
}

} // namespace Envoy

#include "test/integration/integration.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace {

class ApiListenerIntegrationTest : public BaseIntegrationTest,
                                   public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ApiListenerIntegrationTest() : BaseIntegrationTest(GetParam(), bootstrapConfig()) {
    use_lds_ = false;
    autonomous_upstream_ = true;
    defer_listener_finalization_ = true;
  }

  void SetUp() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->mutable_listeners(0)->MergeFrom(
          Server::parseListenerFromV2Yaml(apiListenerConfig()));
    });
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  static std::string bootstrapConfig() {
    // At least one empty filter chain needs to be specified.
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
    )EOF");
  }

  static std::string apiListenerConfig() {
    return R"EOF(
name: api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      virtual_hosts:
        name: integration
        routes:
          route:
            cluster: cluster_0
          match:
            prefix: "/"
        domains: "*"
      name: route_config_0
    http_filters:
      - name: router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      )EOF";
  }

  NiceMock<Http::MockResponseEncoder> stream_encoder_;
};

ACTION_P(Notify, notification) { notification->Notify(); }

INSTANTIATE_TEST_SUITE_P(IpVersions, ApiListenerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ApiListenerIntegrationTest, Basic) {
  BaseIntegrationTest::initialize();
  absl::Notification done;
  test_server_->server().dispatcher().post([this, &done]() -> void {
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener().has_value());
    ASSERT_EQ("api_listener", test_server_->server().listenerManager().apiListener()->get().name());
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener()->get().http().has_value());
    auto& http_api_listener =
        test_server_->server().listenerManager().apiListener()->get().http()->get();

    ON_CALL(stream_encoder_, getStream()).WillByDefault(ReturnRef(stream_encoder_.stream_));
    auto& stream_decoder = http_api_listener.newStream(stream_encoder_);

    // The AutonomousUpstream responds with 200 OK and a body of 10 bytes.
    // In the http1 codec the end stream is encoded with encodeData and 0 bytes.
    Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};
    EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(BufferStringEqual(""), true)).WillOnce(Notify(&done));

    // Send a headers-only request
    stream_decoder.decodeHeaders(
        Http::RequestHeaderMapPtr(new Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/api"}, {":scheme", "http"}, {":authority", "host"}}),
        true);
  });
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

TEST_P(ApiListenerIntegrationTest, DestroyWithActiveStreams) {
  autonomous_allow_incomplete_streams_ = true;
  BaseIntegrationTest::initialize();
  absl::Notification done;

  test_server_->server().dispatcher().post([this, &done]() -> void {
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener().has_value());
    ASSERT_EQ("api_listener", test_server_->server().listenerManager().apiListener()->get().name());
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener()->get().http().has_value());
    auto& http_api_listener =
        test_server_->server().listenerManager().apiListener()->get().http()->get();

    ON_CALL(stream_encoder_, getStream()).WillByDefault(ReturnRef(stream_encoder_.stream_));
    auto& stream_decoder = http_api_listener.newStream(stream_encoder_);

    // Send a headers-only request
    stream_decoder.decodeHeaders(
        Http::RequestHeaderMapPtr(new Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/api"}, {":scheme", "http"}, {":authority", "host"}}),
        false);

    done.Notify();
  });
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));
  // The server should shutdown the ApiListener at the right time during server termination such
  // that no crashes occur if termination happens when the ApiListener still has ongoing streams.
}

} // namespace
} // namespace Envoy

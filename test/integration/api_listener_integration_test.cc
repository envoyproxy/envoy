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
      // currently ApiListener does not trigger this wait
      // https://github.com/envoyproxy/envoy/blob/0b92c58d08d28ba7ef0ed5aaf44f90f0fccc5dce/test/integration/integration.cc#L454
      // Thus, the ApiListener has to be added in addition to the already existing listener in the
      // config.
      bootstrap.mutable_static_resources()->mutable_listeners(0)->MergeFrom(
          Server::parseListenerFromV3Yaml(apiListenerConfig()));
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
  Http::ApiListenerPtr http_api_listener;
  test_server_->server().dispatcher().post([this, &done, &http_api_listener]() -> void {
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener().has_value());
    ASSERT_EQ("api_listener", test_server_->server().listenerManager().apiListener()->get().name());
    http_api_listener =
        test_server_->server().listenerManager().apiListener()->get().createHttpApiListener(
            test_server_->server().dispatcher());
    ASSERT_TRUE(http_api_listener != nullptr);

    ON_CALL(stream_encoder_, getStream()).WillByDefault(ReturnRef(stream_encoder_.stream_));
    auto stream_decoder = http_api_listener->newStreamHandle(stream_encoder_);

    // The AutonomousUpstream responds with 200 OK and a body of 10 bytes.
    // In the http1 codec the end stream is encoded with encodeData and 0 bytes.
    Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};
    EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(BufferStringEqual(""), true)).WillOnce(Notify(&done));

    // Send a headers-only request
    stream_decoder->get()->decodeHeaders(
        Http::RequestHeaderMapPtr(new Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/api"}, {":scheme", "http"}, {":authority", "host"}}),
        true);
  });
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));

  absl::Notification cleanup_done;
  test_server_->server().dispatcher().post([&cleanup_done, &http_api_listener]() {
    // Must be deleted in same thread.
    http_api_listener.reset();
    cleanup_done.Notify();
  });
  ASSERT_TRUE(cleanup_done.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

TEST_P(ApiListenerIntegrationTest, DestroyWithActiveStreams) {
  autonomous_allow_incomplete_streams_ = true;
  BaseIntegrationTest::initialize();
  absl::Notification done;

  test_server_->server().dispatcher().post([this, &done]() -> void {
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener().has_value());
    ASSERT_EQ("api_listener", test_server_->server().listenerManager().apiListener()->get().name());
    auto http_api_listener =
        test_server_->server().listenerManager().apiListener()->get().createHttpApiListener(
            test_server_->server().dispatcher());
    ASSERT_TRUE(http_api_listener != nullptr);

    ON_CALL(stream_encoder_, getStream()).WillByDefault(ReturnRef(stream_encoder_.stream_));
    auto stream_decoder = http_api_listener->newStreamHandle(stream_encoder_);

    // Send a headers-only request
    stream_decoder->get()->decodeHeaders(
        Http::RequestHeaderMapPtr(new Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/api"}, {":scheme", "http"}, {":authority", "host"}}),
        false);

    done.Notify();
    // http_api_listener is destroyed here with an active stream; thus this test verifies that
    // there is no crash when this happens.
    http_api_listener.reset();
  });
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

TEST_P(ApiListenerIntegrationTest, FromWorkerThread) {
  BaseIntegrationTest::initialize();

  absl::Mutex dispatchers_mutex;
  std::vector<Event::Dispatcher*> dispatchers;
  absl::Notification has_dispatcher;
  ThreadLocal::TypedSlotPtr<> slot =
      ThreadLocal::TypedSlot<>::makeUnique(test_server_->server().threadLocal());
  slot->set([&dispatchers_mutex, &dispatchers, &has_dispatcher](
                Event::Dispatcher& dispatcher) -> std::shared_ptr<ThreadLocal::ThreadLocalObject> {
    absl::MutexLock ml(&dispatchers_mutex);
    // A string comparison on thread name seems to be the only way to
    // distinguish worker threads from the main thread with the slots interface.
    if (dispatcher.name() != "main_thread") {
      dispatchers.push_back(&dispatcher);
      has_dispatcher.Notify();
    }
    return nullptr;
  });
  ASSERT_TRUE(has_dispatcher.WaitForNotificationWithTimeout(absl::Seconds(1)));

  absl::Notification done;
  Http::ApiListenerPtr http_api_listener;
  dispatchers[0]->post([this, &done, &http_api_listener, &dispatchers]() -> void {
    ASSERT_TRUE(test_server_->server().listenerManager().apiListener().has_value());
    ASSERT_EQ("api_listener", test_server_->server().listenerManager().apiListener()->get().name());
    http_api_listener =
        test_server_->server().listenerManager().apiListener()->get().createHttpApiListener(
            *dispatchers[0]);
    ASSERT_TRUE(http_api_listener != nullptr);

    ON_CALL(stream_encoder_, getStream()).WillByDefault(ReturnRef(stream_encoder_.stream_));
    auto stream_decoder = http_api_listener->newStreamHandle(stream_encoder_);

    // The AutonomousUpstream responds with 200 OK and a body of 10 bytes.
    // In the http1 codec the end stream is encoded with encodeData and 0 bytes.
    Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};
    EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(_, false));
    EXPECT_CALL(stream_encoder_, encodeData(BufferStringEqual(""), true)).WillOnce(Notify(&done));

    // Send a headers-only request
    stream_decoder->get()->decodeHeaders(
        Http::RequestHeaderMapPtr(new Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/api"}, {":scheme", "http"}, {":authority", "host"}}),
        true);
  });
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));

  absl::Notification cleanup_done;
  dispatchers[0]->post([&cleanup_done, &http_api_listener]() {
    // Must be deleted in same thread.
    http_api_listener.reset();
    cleanup_done.Notify();
  });
  ASSERT_TRUE(cleanup_done.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

} // namespace
} // namespace Envoy

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace {

class ConnectionDurationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          if (enable_connection_duration_timeout_) {
            hcm.mutable_max_connection_duration()->set_seconds(0);
            hcm.mutable_max_connection_duration()->set_nanos(MaxConnectionDurationMs * 1000 * 1000);
          }

          // For validating encode100ContinueHeaders() timer kick.
          hcm.set_proxy_100_continue(true);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  // IntegrationStreamDecoderPtr setupPerStreamIdleTimeoutTest(const char* method = "GET") {
  //   initialize();
  //   fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  //   codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  //   auto encoder_decoder =
  //       codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", method},
  //                                                           {":path", "/test/long/url"},
  //                                                           {":scheme", "http"},
  //                                                           {":authority", "host"}});
  //   request_encoder_ = &encoder_decoder.first;
  //   auto response = std::move(encoder_decoder.second);
  //   AssertionResult result =
  //       fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  //   RELEASE_ASSERT(result, result.message());
  //   result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  //   RELEASE_ASSERT(result, result.message());
  //   result = upstream_request_->waitForHeadersComplete();
  //   RELEASE_ASSERT(result, result.message());
  //   return response;
  // }

  // void sleep() { test_time_.timeSystem().sleep(std::chrono::milliseconds(IdleTimeoutMs / 2)); }

  // void waitForTimeout(IntegrationStreamDecoder& response, absl::string_view stat_name = "",
  //                     absl::string_view stat_prefix = "http.config_test") {
  //   if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
  //     codec_client_->waitForDisconnect();
  //   } else {
  //     response.waitForReset();
  //     codec_client_->close();
  //   }
  //   if (!stat_name.empty()) {
  //     EXPECT_EQ(1, test_server_->counter(fmt::format("{}.{}", stat_prefix, stat_name))->value());
  //   }
  // }

  // TODO(htuch): This might require scaling for TSAN/ASAN/Valgrind/etc. Bump if
  // this is the cause of flakes.
  static constexpr uint64_t MaxConnectionDurationMs = 400;

  bool enable_connection_duration_timeout_{false};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, ConnectionDurationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Tests idle timeout behaviour with single request and validates that idle timer kicks in
// after given timeout.
TEST_P(ConnectionDurationIntegrationTest, TimeoutBasic) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}

TEST_P(ConnectionDurationIntegrationTest, InflightRequest) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(ConnectionDurationIntegrationTest, TimeoutNoHttpRequest) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}
} // namespace
} // namespace Envoy

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace {

class IdleTimeoutIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          if (enable_global_idle_timeout_) {
            hcm.mutable_stream_idle_timeout()->set_seconds(0);
            hcm.mutable_stream_idle_timeout()->set_nanos(TimeoutMs * 1000 * 1000);
          }
          if (enable_per_stream_idle_timeout_) {
            auto* route_config = hcm.mutable_route_config();
            auto* virtual_host = route_config->mutable_virtual_hosts(0);
            auto* route = virtual_host->mutable_routes(0)->mutable_route();
            route->mutable_idle_timeout()->set_seconds(0);
            route->mutable_idle_timeout()->set_nanos(TimeoutMs * 1000 * 1000);
          }
          // For validating encode100ContinueHeaders() timer kick.
          hcm.set_proxy_100_continue(true);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  IntegrationStreamDecoderPtr setupPerStreamIdleTimeoutTest(const char* method = "GET") {
    initialize();
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", method},
                                                            {":path", "/test/long/url"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"}});
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    return response;
  }

  void sleep() { test_time_.timeSystem().sleep(std::chrono::milliseconds(TimeoutMs / 2)); }

  void waitForTimeout(IntegrationStreamDecoder& response) {
    if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
      codec_client_->waitForDisconnect();
    } else {
      response.waitForReset();
      codec_client_->close();
    }
    EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
  }

  // TODO(htuch): This might require scaling for TSAN/ASAN/Valgrind/etc. Bump if
  // this is the cause of flakes.
  static constexpr uint64_t TimeoutMs = 200;
  bool enable_global_idle_timeout_{};
  bool enable_per_stream_idle_timeout_{true};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_CASE_P(Protocols, IdleTimeoutIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

// Per-stream idle timeout after having sent downstream headers.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  auto response = setupPerStreamIdleTimeoutTest();

  waitForTimeout(*response);
  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after having sent downstream head request.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutHeadRequestAfterDownstreamHeadRequest) {
  auto response = setupPerStreamIdleTimeoutTest("HEAD");

  waitForTimeout(*response);
  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_STREQ(fmt::format("{}", strlen("stream timeout")).c_str(),
               response->headers().ContentLength()->value().c_str());
  EXPECT_EQ("", response->body());
}

// Global per-stream idle timeout applies if there is no per-stream idle timeout.
TEST_P(IdleTimeoutIntegrationTest, GlobalPerStreamIdleTimeoutAfterDownstreamHeaders) {
  enable_global_idle_timeout_ = true;
  enable_per_stream_idle_timeout_ = false;
  auto response = setupPerStreamIdleTimeoutTest();

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after having sent downstream headers+body.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterDownstreamHeadersAndBody) {
  auto response = setupPerStreamIdleTimeoutTest();

  sleep();
  codec_client_->sendData(*request_encoder_, 1, false);

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after upstream headers have been sent.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterUpstreamHeaders) {
  auto response = setupPerStreamIdleTimeoutTest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ("", response->body());
}

// Per-stream idle timeout after a sequence of header/data events.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterBidiData) {
  auto response = setupPerStreamIdleTimeoutTest();

  sleep();
  upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});

  sleep();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  sleep();
  upstream_request_->encodeData(1, false);

  sleep();
  codec_client_->sendData(*request_encoder_, 1, false);

  sleep();
  Http::TestHeaderMapImpl request_trailers{{"request1", "trailer1"}, {"request2", "trailer2"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  sleep();
  upstream_request_->encodeData(1, false);

  waitForTimeout(*response);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ("aa", response->body());
}

// Successful request/response when per-stream idle timeout is configured.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutRequestAndResponse) {
  testRouterRequestAndResponseWithBody(1024, 1024, false, nullptr);
}

} // namespace
} // namespace Envoy

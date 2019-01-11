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
            hcm.mutable_stream_idle_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
          }
          if (enable_per_stream_idle_timeout_) {
            auto* route_config = hcm.mutable_route_config();
            auto* virtual_host = route_config->mutable_virtual_hosts(0);
            auto* route = virtual_host->mutable_routes(0)->mutable_route();
            route->mutable_idle_timeout()->set_seconds(0);
            route->mutable_idle_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
          }
          if (enable_request_timeout_) {
            hcm.mutable_request_timeout()->set_seconds(0);
            hcm.mutable_request_timeout()->set_nanos(RequestTimeoutMs * 1000 * 1000);
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

  void sleep() { test_time_.timeSystem().sleep(std::chrono::milliseconds(IdleTimeoutMs / 2)); }

  void waitForTimeout(IntegrationStreamDecoder& response, absl::string_view stat_name = "",
                      absl::string_view stat_prefix = "http.config_test") {
    if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
      codec_client_->waitForDisconnect();
    } else {
      response.waitForReset();
      codec_client_->close();
    }
    if (!stat_name.empty()) {
      EXPECT_EQ(1, test_server_->counter(fmt::format("{}.{}", stat_prefix, stat_name))->value());
    }
  }

  // TODO(htuch): This might require scaling for TSAN/ASAN/Valgrind/etc. Bump if
  // this is the cause of flakes.
  static constexpr uint64_t IdleTimeoutMs = 200;
  static constexpr uint64_t RequestTimeoutMs = 100;
  bool enable_global_idle_timeout_{false};
  bool enable_per_stream_idle_timeout_{false};
  bool enable_request_timeout_{false};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_CASE_P(Protocols, IdleTimeoutIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

// Per-stream idle timeout after having sent downstream headers.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  waitForTimeout(*response, "downstream_rq_idle_timeout");
  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after having sent downstream head request.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutHeadRequestAfterDownstreamHeadRequest) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest("HEAD");

  waitForTimeout(*response, "downstream_rq_idle_timeout");
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
  enable_per_stream_idle_timeout_ = true;
  enable_global_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after having sent downstream headers+body.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterDownstreamHeadersAndBody) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  sleep();
  codec_client_->sendData(*request_encoder_, 1, false);

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after upstream headers have been sent.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterUpstreamHeaders) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ("", response->body());
}

// Per-stream idle timeout after a sequence of header/data events.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterBidiData) {
  enable_per_stream_idle_timeout_ = true;
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

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ("aa", response->body());
}

// Successful request/response when per-stream idle timeout is configured.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutRequestAndResponse) {
  enable_per_stream_idle_timeout_ = true;
  testRouterRequestAndResponseWithBody(1024, 1024, false, nullptr);
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutConfiguredRequestResponse) {
  enable_request_timeout_ = true;
  testRouterHeaderOnlyRequestAndResponse(true, nullptr);
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutConfiguredRequestResponseWithBody) {
  enable_request_timeout_ = true;
  testRouterRequestAndResponseWithBody(1024, 1024, false, nullptr);
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutTriggersOnBodilessPost) {
  enable_request_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");

  waitForTimeout(*response, "downstream_rq_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("request timeout", response->body());
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutUnconfiguredDoesNotTriggerOnBodilessPost) {
  enable_request_timeout_ = false;
  // with no request timeout configured, the idle timeout triggers instead
  enable_per_stream_idle_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_NE("request timeout", response->body());
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutTriggersOnRawIncompleteRequestWithHeaders) {
  // Omitting \r\n\n\n does not indicate incopmlete request in HTTP2
  if (downstreamProtocol() == Envoy::Http::CodecClient::Type::HTTP2) {
    return;
  }
  enable_request_timeout_ = true;

  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  std::string raw_response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1", &raw_response, true);
  EXPECT_THAT(raw_response, testing::HasSubstr("request timeout"));
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutDoesNotTriggerOnRawCompleteRequestWithHeaders) {
  if (downstreamProtocol() == Envoy::Http::CodecClient::Type::HTTP2) {
    return;
  }
  enable_request_timeout_ = true;

  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  std::string raw_response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\n\n\n", &raw_response, true);
  EXPECT_THAT(raw_response, testing::Not(testing::HasSubstr("request timeout")));
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutIsDisarmedByPrematureEncodeHeaders) {
  enable_request_timeout_ = true;
  enable_per_stream_idle_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_FALSE(response->complete());
  EXPECT_NE("request timeout", response->body());
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutIsNotDisarmedByEncode100ContinueHeaders) {
  enable_request_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");
  upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});

  waitForTimeout(*response, "downstream_rq_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("408", response->headers().Status()->value().c_str());
  EXPECT_EQ("request timeout", response->body());
}

// TODO(auni53) create a test filter that hangs and does not send data upstream, which would
// trigger a configured request_timer

} // namespace
} // namespace Envoy

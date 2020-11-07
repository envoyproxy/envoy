#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/test_time.h"

using testing::HasSubstr;

namespace Envoy {
namespace {

class IdleTimeoutIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    useAccessLog("%RESPONSE_CODE_DETAILS%");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
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

            auto* header = virtual_host->mutable_response_headers_to_add()->Add()->mutable_header();
            header->set_key("foo");
            header->set_value("bar");
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
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", method},
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

  void sleep() {
    test_time_.timeSystem().advanceTimeWait(std::chrono::milliseconds(IdleTimeoutMs / 2));
  }

  void waitForTimeout(IntegrationStreamDecoder& response, absl::string_view stat_name = "",
                      absl::string_view stat_prefix = "http.config_test") {
    if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
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
  static constexpr uint64_t IdleTimeoutMs = 400;
  static constexpr uint64_t RequestTimeoutMs = 200;
  bool enable_global_idle_timeout_{false};
  bool enable_per_stream_idle_timeout_{false};
  bool enable_request_timeout_{false};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, IdleTimeoutIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Tests idle timeout behaviour with single request and validates that idle timer kicks in
// after given timeout.
TEST_P(IdleTimeoutIntegrationTest, TimeoutBasic) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* http_protocol_options = cluster->mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
  });
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

  // Do not send any requests and validate if idle time out kicks in.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 1);
}

// Tests idle timeout behaviour with multiple requests and validates that idle timer kicks in
// after both the requests are done.
TEST_P(IdleTimeoutIntegrationTest, IdleTimeoutWithTwoRequests) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* http_protocol_options = cluster->mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Request 2.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 512);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);

  // Do not send any requests and validate if idle time out kicks in.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 1);
}

// Per-stream idle timeout after having sent downstream headers.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  waitForTimeout(*response, "downstream_rq_idle_timeout");
  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
  auto foo = Http::LowerCaseString("foo");
  ASSERT_FALSE(response->headers().get(foo).empty());
  EXPECT_EQ("bar", response->headers().get(foo)[0]->value().getStringView());
  EXPECT_EQ("stream timeout", response->body());

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("stream_idle_timeout"));
}

// Per-stream idle timeout with reads disabled.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutWithLargeBuffer) {
  config_helper_.addFilter(R"EOF(
  name: backpressure-filter
  )EOF");
  enable_per_stream_idle_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());

  // Make sure that for HTTP/1.1 reads are enabled even though the first request
  // ended in the "backed up" state.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  response2->waitForEndStream();
  EXPECT_TRUE(response2->complete());
}

// Per-stream idle timeout after having sent downstream head request.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutHeadRequestAfterDownstreamHeadRequest) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest("HEAD");

  waitForTimeout(*response, "downstream_rq_idle_timeout");
  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ(fmt::format("{}", strlen("stream timeout")),
            response->headers().getContentLengthValue());
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
  EXPECT_EQ("408", response->headers().getStatusValue());
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
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ("stream timeout", response->body());
}

// Per-stream idle timeout after upstream headers have been sent.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterUpstreamHeaders) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("", response->body());
}

// Per-stream idle timeout after a sequence of header/data events.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutAfterBidiData) {
  enable_per_stream_idle_timeout_ = true;
  auto response = setupPerStreamIdleTimeoutTest();

  sleep();
  upstream_request_->encode100ContinueHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});

  sleep();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  sleep();
  upstream_request_->encodeData(1, false);

  sleep();
  codec_client_->sendData(*request_encoder_, 1, false);

  sleep();
  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  sleep();
  upstream_request_->encodeData(1, false);

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("aa", response->body());
}

// Successful request/response when per-stream idle timeout is configured.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutRequestAndResponse) {
  enable_per_stream_idle_timeout_ = true;
  testRouterRequestAndResponseWithBody(1024, 1024, false);
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutConfiguredRequestResponse) {
  enable_request_timeout_ = true;
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutConfiguredRequestResponseWithBody) {
  enable_request_timeout_ = true;
  testRouterRequestAndResponseWithBody(1024, 1024, false);
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutTriggersOnBodilessPost) {
  enable_request_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");

  waitForTimeout(*response, "downstream_rq_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
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
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_NE("request timeout", response->body());
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutTriggersOnRawIncompleteRequestWithHeaders) {
  // Omitting \r\n\r\n does not indicate incomplete request in HTTP2
  if (downstreamProtocol() == Envoy::Http::CodecClient::Type::HTTP2) {
    return;
  }
  enable_request_timeout_ = true;

  initialize();

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

  std::string raw_response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\n\r\n", &raw_response, true);
  EXPECT_THAT(raw_response, testing::Not(testing::HasSubstr("request timeout")));
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutIsDisarmedByPrematureEncodeHeaders) {
  enable_request_timeout_ = true;
  enable_per_stream_idle_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response);

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_FALSE(response->complete());
  EXPECT_NE("request timeout", response->body());
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutIsNotDisarmedByEncode100ContinueHeaders) {
  enable_request_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");
  upstream_request_->encode100ContinueHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});

  waitForTimeout(*response, "downstream_rq_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ("request timeout", response->body());
}

// TODO(auni53) create a test filter that hangs and does not send data upstream, which would
// trigger a configured request_timer

} // namespace
} // namespace Envoy

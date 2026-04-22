#include "envoy/access_log/access_log.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/upstreams/tcp/v3/tcp_protocol_options.pb.h"
#include "envoy/server/filter_config.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

using testing::HasSubstr;

namespace Envoy {
namespace {

class IdleTimeoutIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.disableDelayClose();
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
          if (enable_route_timeout_) {
            auto* route_config = hcm.mutable_route_config();
            auto* virtual_host = route_config->mutable_virtual_hosts(0);
            auto* route = virtual_host->mutable_routes(0)->mutable_route();
            route->mutable_timeout()->set_seconds(0);
            route->mutable_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
          }
          if (enable_request_timeout_) {
            hcm.mutable_request_timeout()->set_seconds(0);
            hcm.mutable_request_timeout()->set_nanos(RequestTimeoutMs * 1000 * 1000);
          }
          if (enable_per_try_idle_timeout_) {
            auto* route_config = hcm.mutable_route_config();
            auto* virtual_host = route_config->mutable_virtual_hosts(0);
            auto* route = virtual_host->mutable_routes(0)->mutable_route();
            auto* retry_policy = route->mutable_retry_policy();
            retry_policy->mutable_per_try_idle_timeout()->set_seconds(0);
            retry_policy->mutable_per_try_idle_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
          }

          // For validating encode1xxHeaders() timer kick.
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
                                                                   {":authority", "sni.lyft.com"}});
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

  IntegrationStreamDecoderPtr setupPerTryIdleTimeoutTest(const char* method = "GET") {
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    auto response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", method},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"}});
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    return response;
  }

  void sleep() {
    test_time_.timeSystem().advanceTimeWait(std::chrono::milliseconds(IdleTimeoutMs / 2));
  }

  void waitForTimeout(IntegrationStreamDecoder& response, absl::string_view stat_name = "",
                      absl::string_view stat_prefix = "http.config_test") {
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    } else {
      ASSERT_TRUE(response.waitForAnyTermination());
      codec_client_->close();
    }
    if (!stat_name.empty()) {
      EXPECT_EQ(1, test_server_->counter(fmt::format("{}.{}", stat_prefix, stat_name))->value());
    }
  }

  static constexpr uint64_t IdleTimeoutMs = 300 * TIMEOUT_FACTOR;
  static constexpr uint64_t RequestTimeoutMs = 200;
  bool enable_global_idle_timeout_{false};
  bool enable_per_stream_idle_timeout_{false};
  bool enable_request_timeout_{false};
  bool enable_route_timeout_{false};
  bool enable_per_try_idle_timeout_{false};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, IdleTimeoutIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Tests idle timeout behaviour with single request and validates that idle timer kicks in
// after given timeout.
TEST_P(IdleTimeoutIntegrationTest, TimeoutBasic) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

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
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Request 2.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 512);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);

  // Do not send any requests and validate if idle time out kicks in.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 1);
}

// Max connection duration reached after a connection is created.
TEST_P(IdleTimeoutIntegrationTest, MaxConnectionDurationBasic) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    auto* max_connection_duration = http_protocol_options->mutable_max_connection_duration();
    max_connection_duration->set_seconds(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Do not send any requests and validate that the max connection duration is reached.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_max_duration_reached", 1);
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

TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutDuringHostSelection) {
  enable_per_stream_idle_timeout_ = true;
  config_helper_.setAsyncLb(true); // Set the lb to hang during host selection.

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("stream_idle_timeout"));
}

// Per-stream idle timeout after having sent downstream headers.
TEST_P(IdleTimeoutIntegrationTest, IdleStreamTimeoutWithRouteReselect) {
  enable_per_stream_idle_timeout_ = true;
  config_helper_.prependFilter(R"EOF(
    name: set-route-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SetRouteFilterConfig
      cluster_override: cluster_0
      idle_timeout_override:
        seconds: 5
  )EOF");

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Sleep past the default timeout.
  sleep();
  sleep();
  sleep();

  EXPECT_FALSE(response->complete());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Per-stream idle timeout with reads disabled.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutWithLargeBuffer) {
  config_helper_.prependFilter(R"EOF(
  name: backpressure-filter
  )EOF");
  enable_per_stream_idle_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Make sure that for HTTP/1.1 reads are enabled even though the first request
  // ended in the "backed up" state.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
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

TEST_P(IdleTimeoutIntegrationTest, ResponseTimeout) {
  enable_route_timeout_ = true;
  initialize();

  // Lock up fake upstream so that it won't accept connections.
  absl::MutexLock l(fake_upstreams_[0]->lock());

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("response_timeout"));
}

// Per-try idle timeout after upstream headers have been sent.
TEST_P(IdleTimeoutIntegrationTest, PerTryIdleTimeoutAfterUpstreamHeaders) {
  enable_per_try_idle_timeout_ = true;
  auto response = setupPerTryIdleTimeoutTest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  waitForTimeout(*response);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_per_try_idle_timeout", 1);

  EXPECT_TRUE(upstream_request_->complete());
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
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});

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
  // Omitting \r\n\r\n does not indicate incomplete request in HTTP2/3
  if (downstreamProtocol() != Envoy::Http::CodecType::HTTP1) {
    return;
  }
  enable_request_timeout_ = true;

  initialize();

  std::string raw_response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1", &raw_response);
  EXPECT_THAT(raw_response, testing::HasSubstr("request timeout"));
}

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutDoesNotTriggerOnRawCompleteRequestWithHeaders) {
  if (downstreamProtocol() != Envoy::Http::CodecType::HTTP1) {
    return;
  }
  enable_request_timeout_ = true;

  initialize();

  std::string raw_response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.1\r\n\r\n", &raw_response);
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

TEST_P(IdleTimeoutIntegrationTest, RequestTimeoutIsNotDisarmedByEncode1xxHeaders) {
  enable_request_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest("POST");
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});

  waitForTimeout(*response, "downstream_rq_timeout");

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ("request timeout", response->body());
}

// Per-stream idle timeout reset from within a filter.
TEST_P(IdleTimeoutIntegrationTest, PerStreamIdleTimeoutResetFromFilter) {
  config_helper_.prependFilter(R"EOF(
  name: reset-idle-timer-filter
  )EOF");
  enable_per_stream_idle_timeout_ = true;

  auto response = setupPerStreamIdleTimeoutTest();

  sleep();
  codec_client_->sendData(*request_encoder_, 1, false);

  // Two sleeps should trigger the timer, as each advances time by timeout / 2. However, the data
  // frame above would have caused the filter to reset the timer. Thus, the stream should not be
  // reset yet.
  sleep();

  EXPECT_FALSE(response->complete());

  sleep();

  waitForTimeout(*response, "downstream_rq_idle_timeout");

  // Now the timer should have triggered.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());
  EXPECT_EQ("stream timeout", response->body());
}

// TODO(auni53) create a test filter that hangs and does not send data upstream, which would
// trigger a configured request_timer

class UpstreamIdleTimeoutVerifierFilter : public Network::ReadFilter,
                                          public Network::ConnectionCallbacks,
                                          public AccessLog::Instance {
public:
  UpstreamIdleTimeoutVerifierFilter(Stats::Scope& scope) : scope_(scope) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    if (!callbacks_->connection().streamInfo().upstreamInfo()) {
      callbacks_->connection().streamInfo().setUpstreamInfo(
          std::make_shared<StreamInfo::UpstreamInfoImpl>());
    }
    return Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks.connection().addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      Formatter::Context context(nullptr, nullptr, nullptr, {},
                                 Envoy::Formatter::AccessLogType::NotSet, nullptr);
      log(context, callbacks_->connection().streamInfo());
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // AccessLog::Instance
  void log(const AccessLog::LogContext& context,
           const StreamInfo::StreamInfo& stream_info) override {
    auto formatter_or_error = Formatter::FormatterImpl::create("%UPSTREAM_LOCAL_CLOSE_REASON%");
    auto formatter = std::move(*formatter_or_error);
    std::string reason = formatter->format(context, stream_info);
    if (reason == "on_idle_timeout" || reason == "tcp_session_idle_timeout") {
      scope_.counterFromString("upstream_idle_timeout_verifier.on_idle_timeout").inc();
    }
  }

  Stats::Scope& scope_;
  Network::ReadFilterCallbacks* callbacks_{};
};

class UpstreamIdleTimeoutVerifierFilterFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    Stats::Scope& scope = context.scope();
    return [&scope](Network::FilterManager& filter_manager) {
      auto filter = std::make_shared<UpstreamIdleTimeoutVerifierFilter>(scope);
      filter_manager.addReadFilter(filter);
      filter_manager.addAccessLogHandler(filter);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.test.upstream_idle_timeout_verifier"; }
};

class UpstreamHttpIdleTimeoutTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  UpstreamHttpIdleTimeoutTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()), register_factory_(factory_) {}

  void initialize() override {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      // Add custom upstream filter
      auto* filter = cluster->add_filters();
      filter->set_name("envoy.test.upstream_idle_timeout_verifier");
      filter->mutable_typed_config()->PackFrom(Protobuf::Struct());

      // Set idle timeout
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
      auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
      auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
      idle_time_out->set_seconds(1);
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    });
    HttpIntegrationTest::initialize();
  }

  UpstreamIdleTimeoutVerifierFilterFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>
      register_factory_;
};

INSTANTIATE_TEST_SUITE_P(Params, UpstreamHttpIdleTimeoutTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UpstreamHttpIdleTimeoutTest, UpstreamConnectionIdleTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // Close downstream connection
  codec_client_->close();

  // Wait for upstream idle timeout
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Wait for stat
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_idle_timeout_verifier.on_idle_timeout",
                                 1);
}

} // namespace
} // namespace Envoy

#include "test/integration/integration_test.h"

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/filters/process_context_filter.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using Envoy::Http::Headers;
using Envoy::Http::HeaderValueOf;
using Envoy::Http::HttpStatusIs;
using testing::EndsWith;
using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace {

std::string normalizeDate(const std::string& s) {
  const std::regex date_regex("date:[^\r]+");
  return std::regex_replace(s, date_regex, "date: Mon, 01 Jan 2017 00:00:00 GMT");
}

void setDisallowAbsoluteUrl(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  hcm.mutable_http_protocol_options()->mutable_allow_absolute_url()->set_value(false);
};

void setAllowHttp10WithDefaultHost(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  hcm.mutable_http_protocol_options()->set_accept_http_10(true);
  hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
}

} // namespace

INSTANTIATE_TEST_SUITE_P(IpVersions, IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that we gracefully handle an invalid pre-bind socket option when using reuse port.
TEST_P(IntegrationTest, BadPrebindSocketOptionWithReusePort) {
  // Reserve a port that we can then use on the integration listener with reuse port.
  auto addr_socket =
      Network::Test::bindFreeLoopbackPort(version_, Network::Socket::Type::Stream, true);
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_reuse_port(true);
    listener->mutable_address()->mutable_socket_address()->set_port_value(
        addr_socket.second->localAddress()->ip()->port());
    auto socket_option = listener->add_socket_options();
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_PREBIND);
    socket_option->set_level(10000);     // Invalid level.
    socket_option->set_int_value(10000); // Invalid value.
  });
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Verify that we gracefully handle an invalid post-bind socket option when using reuse port.
TEST_P(IntegrationTest, BadPostbindSocketOptionWithReusePort) {
  // Reserve a port that we can then use on the integration listener with reuse port.
  auto addr_socket =
      Network::Test::bindFreeLoopbackPort(version_, Network::Socket::Type::Stream, true);
  // Do not wait for listeners to start as the listener will fail.
  defer_listener_finalization_ = true;

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_reuse_port(true);
    listener->mutable_address()->mutable_socket_address()->set_port_value(
        addr_socket.second->localAddress()->ip()->port());
    auto socket_option = listener->add_socket_options();
    socket_option->set_state(envoy::config::core::v3::SocketOption::STATE_BOUND);
    socket_option->set_level(10000);     // Invalid level.
    socket_option->set_int_value(10000); // Invalid value.
  });
  initialize();
  test_server_->waitForCounterGe("listener_manager.listener_create_failure", 1);
}

// Make sure we have correctly specified per-worker performance stats.
TEST_P(IntegrationTest, PerWorkerStatsAndBalancing) {
  concurrency_ = 2;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_connection_balance_config()->mutable_exact_balance();
  });
  initialize();

  // Per-worker listener stats.
  auto check_listener_stats = [this](uint64_t cx_active, uint64_t cx_total) {
    if (GetParam() == Network::Address::IpVersion::v4) {
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.127.0.0.1_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.127.0.0.1_0.worker_1.downstream_cx_total", cx_total);
    } else {
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_0.downstream_cx_active", cx_active);
      test_server_->waitForGaugeEq("listener.[__1]_0.worker_1.downstream_cx_active", cx_active);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_0.downstream_cx_total", cx_total);
      test_server_->waitForCounterEq("listener.[__1]_0.worker_1.downstream_cx_total", cx_total);
    }
  };
  check_listener_stats(0, 0);

  // Main thread admin listener stats.
  EXPECT_NE(nullptr, test_server_->counter("listener.admin.main_thread.downstream_cx_total"));

  // Per-thread watchdog stats.
  EXPECT_NE(nullptr, test_server_->counter("server.main_thread.watchdog_miss"));
  EXPECT_NE(nullptr, test_server_->counter("server.worker_0.watchdog_miss"));
  EXPECT_NE(nullptr, test_server_->counter("server.worker_1.watchdog_miss"));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  check_listener_stats(1, 1);

  codec_client_->close();
  codec_client2->close();
  check_listener_stats(0, 1);
}

TEST_P(IntegrationTest, RouterDirectResponse) {
  const std::string body = "Response body";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", body);
  static const std::string domain("direct.example.com");
  static const std::string prefix("/");
  static const Http::Code status(Http::Code::OK);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("x-additional-header");
        header_value_option->mutable_header()->set_value("example-value");
        header_value_option->mutable_append()->set_value(false);
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-type");
        header_value_option->mutable_header()->set_value("text/html");
        header_value_option->mutable_append()->set_value(false);
        auto* virtual_host = route_config->add_virtual_hosts();
        virtual_host->set_name(domain);
        virtual_host->add_domains(domain);
        virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
        virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
            static_cast<uint32_t>(status));
        virtual_host->mutable_routes(0)->mutable_direct_response()->mutable_body()->set_filename(
            file_path);
      });
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "direct.example.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("example-value", response->headers()
                                 .get(Envoy::Http::LowerCaseString("x-additional-header"))
                                 ->value()
                                 .getStringView());
  EXPECT_EQ("text/html", response->headers().getContentTypeValue());
  EXPECT_EQ(body, response->body());
}

TEST_P(IntegrationTest, ConnectionClose) {
  config_helper_.addFilter(ConfigHelper::defaultHealthCheckFilter());
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":authority", "host"},
                                                                          {"connection", "close"}});
  response->waitForEndStream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, LargeFlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(128 * 1024, 128 * 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyAndContentLengthNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, true);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyAndContentLengthNoBuffer) {
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, LargeFlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.setBufferLimits(128 * 1024, 128 * 1024);
  testRouterRequestAndResponseWithBody(10 * 1024 * 1024, 10 * 1024 * 1024, false, true);
}

TEST_P(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/9508
TEST_P(IntegrationTest, ResponseFramedByConnectionCloseWithReadLimits) {
  // Set a small buffer limit on the downstream in order to trigger a call to trigger readDisable on
  // the upstream when proxying the response. Upstream limit needs to be larger so that
  // RawBufferSocket::doRead reads the response body and detects the upstream close in the same call
  // stack.
  config_helper_.setBufferLimits(100000, 1);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // Disable chunk encoding to trigger framing by connection close.
  upstream_request_->http1StreamEncoderOptions().value().get().disableChunkEncoding();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_EQ(512, response->body().size());
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(IntegrationTest, EnvoyProxyingEarly100ContinueWithEncoderFilter) {
  testEnvoyProxying1xx(true, true);
}

TEST_P(IntegrationTest, EnvoyProxyingLate100ContinueWithEncoderFilter) {
  testEnvoyProxying1xx(false, true);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10923.
TEST_P(IntegrationTest, EnvoyProxying100ContinueWithDecodeDataPause) {
  config_helper_.addFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
  )EOF");
  testEnvoyProxying1xx(true);
}

// This is a regression for https://github.com/envoyproxy/envoy/issues/2715 and validates that a
// pending request is not sent on a connection that has been half-closed.
TEST_P(IntegrationTest, UpstreamDisconnectWithTwoRequests) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    // Ensure we only have one connection upstream, one request active at a time.
    cluster->mutable_max_requests_per_connection()->set_value(1);
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_connections()->set_value(1);
  });
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // Request 2.
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client2->makeRequestWithBody(default_request_headers_, 512);

  // Validate one request active, the other pending.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 1);

  // Response 1.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Response 2.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);
  response2->waitForEndStream();
  codec_client2->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 2);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);
}

// Test hitting the bridge filter with too many response bytes to buffer. Given
// the headers are not proxied, the connection manager will send a local error reply.
TEST_P(IntegrationTest, HittingGrpcFilterLimitBufferingHeaders) {
  config_helper_.addFilter(
      "{ name: grpc_http1_bridge, typed_config: { \"@type\": "
      "type.googleapis.com/envoy.config.filter.http.grpc_http1_bridge.v2.Config } }");
  config_helper_.setBufferLimits(1024, 1024);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"},
                                     {"x-envoy-retry-grpc-on", "cancelled"}});
  waitForNextUpstreamRequest();

  // Send the overly large response. Because the grpc_http1_bridge filter buffers and buffer
  // limits are exceeded, this will be translated into an unknown gRPC error.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Headers::get().GrpcStatus, "2")); // Unknown gRPC error
}

TEST_P(IntegrationTest, TestSmuggling) {
  initialize();
  const std::string smuggled_request = "GET / HTTP/1.1\r\nHost: disallowed\r\n\r\n";
  ASSERT_EQ(smuggled_request.length(), 36);
  // Make sure the http parser rejects having content-length and transfer-encoding: chunked
  // on the same request, regardless of order and spacing.
  {
    std::string response;
    const std::string full_request =
        "GET / HTTP/1.1\r\nHost: host\r\ncontent-length: 36\r\ntransfer-encoding: chunked\r\n\r\n" +
        smuggled_request;
    sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);
    EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  }
  {
    std::string response;
    const std::string request = "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: chunked "
                                "\r\ncontent-length: 36\r\n\r\n" +
                                smuggled_request;
    sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
    EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  }
  {
    std::string response;
    const std::string request = "GET / HTTP/1.1\r\nHost: host\r\ntransfer-encoding: "
                                "identity,chunked \r\ncontent-length: 36\r\n\r\n" +
                                smuggled_request;
    sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response, false);
    EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  }
}

TEST_P(IntegrationTest, BadFirstline) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "hello", &response);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, MissingDelimiter) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\nHost: host\r\nfoo bar\r\n\r\n", &response);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http1.codec_error"));
}

TEST_P(IntegrationTest, InvalidCharacterInFirstline) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GE(T / HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, InvalidVersion) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.01\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
}

// Expect that malformed trailers to break the connection
TEST_P(IntegrationTest, BadTrailer) {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "POST / HTTP/1.1\r\n"
                                "Host: host\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "4\r\n"
                                "body\r\n0\r\n"
                                "badtrailer\r\n\r\n",
                                &response);

  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
}

// Expect malformed headers to break the connection
TEST_P(IntegrationTest, BadHeader) {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "POST / HTTP/1.1\r\n"
                                "Host: host\r\n"
                                "badHeader\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "4\r\n"
                                "body\r\n0\r\n\r\n",
                                &response);

  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_P(IntegrationTest, Http10Disabled) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

TEST_P(IntegrationTest, Http10DisabledWithUpgrade) {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\nUpgrade: h2c\r\n\r\n",
                                &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

// Turn HTTP/1.0 support on and verify 09 style requests work.
TEST_P(IntegrationTest, Http09Enabled) {
  useAccessLog();
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET /\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");

  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("HTTP/1.0"));
}

TEST_P(IntegrationTest, Http09WithKeepalive) {
  useAccessLog();
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "0"}})));
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET /\r\nConnection: keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: keep-alive\r\n"));
}

// Turn HTTP/1.0 support on and verify the request is proxied and the default host is sent upstream.
TEST_P(IntegrationTest, Http10Enabled) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");

  sendRawHttpAndWaitForResponse(lookupPort("http"), "HEAD / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));
}

TEST_P(IntegrationTest, TestInlineHeaders) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\n"
                                "Host: foo.com\r\n"
                                "Foo: bar\r\n"
                                "User-Agent: public\r\n"
                                "User-Agent: 123\r\n"
                                "Eep: baz\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
  EXPECT_EQ(upstream_headers->get_("User-Agent"), "public,123");
  ASSERT_TRUE(upstream_headers->get(Envoy::Http::LowerCaseString("foo")) != nullptr);
  EXPECT_EQ("bar",
            upstream_headers->get(Envoy::Http::LowerCaseString("foo"))->value().getStringView());
  ASSERT_TRUE(upstream_headers->get(Envoy::Http::LowerCaseString("eep")) != nullptr);
  EXPECT_EQ("baz",
            upstream_headers->get(Envoy::Http::LowerCaseString("eep"))->value().getStringView());
}

// Verify for HTTP/1.0 a keep-alive header results in no connection: close.
// Also verify existing host headers are passed through for the HTTP/1.0 case.
// This also regression tests proper handling of trailing whitespace after key
// values, specifically the host header.
TEST_P(IntegrationTest, Http10WithHostandKeepAliveAndLwsNoContentLength) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.0\r\nHost: foo.com \r\nConnection:Keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("connection: keep-alive")));
  EXPECT_THAT(response, Not(HasSubstr("content-length:")));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
}

TEST_P(IntegrationTest, Http10WithHostandKeepAliveAndContentLengthAndLws) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "10"}})));
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.0\r\nHost: foo.com \r\nConnection:Keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("connection: close")));
  EXPECT_THAT(response, HasSubstr("connection: keep-alive"));
  EXPECT_THAT(response, HasSubstr("content-length:"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));
}

TEST_P(IntegrationTest, Pipeline) {
  autonomous_upstream_ = true;
  initialize();
  std::string response;

  auto connection = createConnectionDriver(
      lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\n\r\nGET / HTTP/1.1\r\n\r\n",
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });
  // First response should be success.
  while (response.find("200") == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));

  // Second response should be 400 (no host)
  while (response.find("400") == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  connection->close();
}

// Checks to ensure that we reject the third request that is pipelined in the
// same request
TEST_P(IntegrationTest, PipelineWithTrailers) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();
  std::string response;

  std::string good_request("POST / HTTP/1.1\r\n"
                           "Host: host\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "4\r\n"
                           "body\r\n0\r\n"
                           "trailer1:t2\r\n"
                           "trailer2:t3\r\n"
                           "\r\n");

  std::string bad_request("POST / HTTP/1.1\r\n"
                          "Host: host\r\n"
                          "Transfer-Encoding: chunked\r\n\r\n"
                          "4\r\n"
                          "body\r\n0\r\n"
                          "trailer1\r\n"
                          "trailer2:t3\r\n"
                          "\r\n");

  auto connection = createConnectionDriver(
      lookupPort("http"), absl::StrCat(good_request, good_request, bad_request),
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });

  // First response should be success.
  size_t pos;
  while ((pos = response.find("200")) == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));
  while (response.find("200", pos + 1) == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  while (response.find("400") == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));
  connection->close();
}

// Add a pipeline test where complete request headers in the first request merit
// an inline sendLocalReply to make sure the "kick" works under the call stack
// of dispatch as well as when a response is proxied from upstream.
TEST_P(IntegrationTest, PipelineInline) {
  // When deprecating this flag, set hcm.mutable_stream_error_on_invalid_http_message true.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.hcm_stream_error_on_invalid_message",
                                    "false");

  autonomous_upstream_ = true;
  initialize();
  std::string response;

  auto connection = createConnectionDriver(
      lookupPort("http"), "GET / HTTP/1.1\r\n\r\nGET / HTTP/1.0\r\n\r\n",
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });

  while (response.find("400") == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 400 Bad Request\r\n"));

  while (response.find("426") == std::string::npos) {
    connection->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 426 Upgrade Required\r\n"));
  connection->close();
}

TEST_P(IntegrationTest, NoHost) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(IntegrationTest, BadPath) {
  config_helper_.addConfigModifier(&setDisallowAbsoluteUrl);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n", &response,
                                true);
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

TEST_P(IntegrationTest, AbsolutePath) {
  // Configure www.redirect.com to send a redirect, and ensure the redirect is
  // encountered via absolute URL.
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_FALSE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

TEST_P(IntegrationTest, AbsolutePathWithPort) {
  // Configure www.namewithport.com:1234 to send a redirect, and ensure the redirect is
  // encountered via absolute URL with a port.
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET http://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n",
      &response, true);
  EXPECT_FALSE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

TEST_P(IntegrationTest, AbsolutePathWithoutPort) {
  // Add a restrictive default match, to avoid the request hitting the * / catchall.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  // Set a matcher for www.namewithport.com:1234 and verify http://www.namewithport.com does not
  // match
  auto host = config_helper_.createVirtualHost("www.namewithport.com:1234", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.namewithport.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0) << response;
}

// Ensure that connect behaves the same with allow_absolute_url enabled and without
TEST_P(IntegrationTest, Connect) {
  const std::string& request = "CONNECT www.somewhere.com:80 HTTP/1.1\r\n\r\n";
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // Clone the whole listener.
    auto static_resources = bootstrap.mutable_static_resources();
    auto* old_listener = static_resources->mutable_listeners(0);
    auto* cloned_listener = static_resources->add_listeners();
    cloned_listener->CopyFrom(*old_listener);
    old_listener->set_name("http_forward");
  });
  // Set the first listener to disallow absolute URLs.
  config_helper_.addConfigModifier(&setDisallowAbsoluteUrl);
  initialize();

  std::string response1;
  sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response1, true);

  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http_forward"), request.c_str(), &response2, true);

  EXPECT_EQ(normalizeDate(response1), normalizeDate(response2));
}

TEST_P(IntegrationTest, UpstreamProtocolError) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test/long/url"}, {":authority", "host"}});
  auto response = std::move(encoder_decoder.second);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  // TODO(mattklein123): Waiting for exact amount of data is a hack. This needs to
  // be fixed.
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(187, &data));
  ASSERT_TRUE(fake_upstream_connection->write("bad protocol data!"));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(IntegrationTest, TestHead) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl head_request{{":method", "HEAD"},
                                              {":path", "/test/long/url"},
                                              {":scheme", "http"},
                                              {":authority", "host"}};

  // Without an explicit content length, assume we chunk for HTTP/1.1
  auto response = sendRequestAndWaitForResponse(head_request, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_EQ(response->headers().ContentLength(), nullptr);
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Headers::get().TransferEncoding,
                            Http::Headers::get().TransferEncodingValues.Chunked));
  EXPECT_EQ(0, response->body().size());

  // Preserve explicit content length.
  Http::TestResponseHeaderMapImpl content_length_response{{":status", "200"},
                                                          {"content-length", "12"}};
  response = sendRequestAndWaitForResponse(head_request, 0, content_length_response, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().ContentLength, "12"));
  EXPECT_EQ(response->headers().TransferEncoding(), nullptr);
  EXPECT_EQ(0, response->body().size());
}

// The Envoy HTTP/1.1 codec ASSERTs that T-E headers are cleared in
// encodeHeaders, so to test upstreams explicitly sending T-E: chunked we have
// to send raw HTTP.
TEST_P(IntegrationTest, TestHeadWithExplicitTE) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("HEAD / HTTP/1.1\r\nHost: host\r\n\r\n"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));

  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  std::string response = tcp_client->data();

  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("content-length")));
  EXPECT_THAT(response, HasSubstr("transfer-encoding: chunked\r\n"));
  EXPECT_THAT(response, EndsWith("\r\n\r\n"));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(IntegrationTest, TestBind) {
  std::string address_string;
  if (GetParam() == Network::Address::IpVersion::v4) {
    address_string = TestUtility::getIpv4Loopback();
  } else {
    address_string = "::1";
  }
  config_helper_.setSourceAddress(address_string);
  useAccessLog("%UPSTREAM_LOCAL_ADDRESS%\n");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                         1024);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_NE(fake_upstream_connection_, nullptr);
  std::string address =
      fake_upstream_connection_->connection().remoteAddress()->ip()->addressAsString();
  EXPECT_EQ(address, address_string);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_NE(upstream_request_, nullptr);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  cleanupUpstreamAndDownstream();
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr(address_string));
}

TEST_P(IntegrationTest, TestFailedBind) {
  config_helper_.setSourceAddress("8.8.8.8");

  initialize();
  // Envoy will create and close some number of connections when trying to bind.
  // Make sure they don't cause assertion failures when we ignore them.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // With no ability to successfully bind on an upstream connection Envoy should
  // send a 500.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-upstream-rq-timeout-ms", "1000"}});
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("503"));
  EXPECT_LT(0, test_server_->counter("cluster.cluster_0.bind_errors")->value());
}

ConfigHelper::HttpModifierFunction setVia(const std::string& via) {
  return
      [via](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.set_via(via); };
}

// Validate in a basic header-only request we get via header insertion.
TEST_P(IntegrationTest, ViaAppendHeaderOnly) {
  config_helper_.addConfigModifier(setVia("bar"));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":authority", "host"},
                                     {"via", "foo"},
                                     {"connection", "close"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().Via, "foo, bar"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().Via, "bar"));
}

// Validate that 100-continue works as expected with via header addition on both request and
// response path.
TEST_P(IntegrationTest, ViaAppendWith100Continue) {
  config_helper_.addConfigModifier(setVia("foo"));
  testEnvoyHandling100Continue(false, "foo");
}

// Test delayed close semantics for downstream HTTP/1.1 connections. When an early response is
// sent by Envoy, it will wait for response acknowledgment (via FIN/RST) from the client before
// closing the socket (with a timeout for ensuring cleanup).
TEST_P(IntegrationTest, TestDelayedConnectionTeardownOnGracefulClose) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
  // This test will trigger an early 413 Payload Too Large response due to buffer limits being
  // exceeded. The following filter is needed since the router filter will never trigger a 413.
  config_helper_.addFilter("{ name: encoder-decoder-buffer-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  // With no delayed close processing, Envoy will close the connection immediately after flushing
  // and this should instead return true.
  EXPECT_FALSE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));

  // Issue a local close and check that the client did not pick up a remote close which can happen
  // when delayed close semantics are disabled.
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::LocalClose);
}

// Test configuration of the delayed close timeout on downstream HTTP/1.1 connections. A value of 0
// disables delayed close processing.
TEST_P(IntegrationTest, TestDelayedConnectionTeardownConfig) {
  config_helper_.addFilter("{ name: encoder-decoder-buffer-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(0); });
  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  // There is a potential race in the client's response processing when delayed close logic is
  // disabled in Envoy (see https://github.com/envoyproxy/envoy/issues/2929). Depending on timing,
  // a client may receive an RST prior to reading the response data from the socket, which may clear
  // the receive buffers. Also, clients which don't flush the receive buffer upon receiving a remote
  // close may also lose data (Envoy is susceptible to this).
  // Therefore, avoid checking response code/payload here and instead simply look for the remote
  // close.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
}

// Test that delay closed connections are eventually force closed when the timeout triggers.
TEST_P(IntegrationTest, TestDelayedConnectionTeardownTimeoutTrigger) {
  config_helper_.addFilter("{ name: encoder-decoder-buffer-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 200ms.
        hcm.mutable_delayed_close_timeout()->set_nanos(200000000);
      });

  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  // The delayed close timeout should trigger since client is not closing the connection.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(2000)));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

// Test that if the route cache is cleared, it doesn't cause problems.
TEST_P(IntegrationTest, TestClearingRouteCacheFilter) {
  config_helper_.addFilter("{ name: clear-route-cache, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
}

// Test that if no connection pools are free, Envoy fails to establish an upstream connection.
TEST_P(IntegrationTest, NoConnectionPoolsFree) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    // Somewhat contrived with 0, but this is the simplest way to test right now.
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_connection_pools()->set_value(0);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);

  // Validate none active.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 0);

  response->waitForEndStream();

  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_503", 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_pool_overflow")->value(), 1);
}

TEST_P(IntegrationTest, ProcessObjectHealthy) {
  config_helper_.addFilter("{ name: process-context-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");

  ProcessObjectForFilter healthy_object(true);
  process_object_ = healthy_object;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":authority", "host"},
                                                                          {"connection", "close"}});
  response->waitForEndStream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

TEST_P(IntegrationTest, ProcessObjectUnealthy) {
  config_helper_.addFilter("{ name: process-context-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");

  ProcessObjectForFilter unhealthy_object(false);
  process_object_ = unhealthy_object;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":authority", "host"},
                                                                          {"connection", "close"}});
  response->waitForEndStream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("500"));
}

TEST_P(IntegrationTest, TrailersDroppedDuringEncoding) { testTrailers(10, 10, false, false); }

TEST_P(IntegrationTest, TrailersDroppedUpstream) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  testTrailers(10, 10, false, false);
}

TEST_P(IntegrationTest, TrailersDroppedDownstream) {
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  testTrailers(10, 10, false, false);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamEndpointIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UpstreamEndpointIntegrationTest, TestUpstreamEndpointAddress) {
  initialize();
  EXPECT_STREQ(fake_upstreams_[0]->localAddress()->ip()->addressAsString().c_str(),
               Network::Test::getLoopbackAddressString(GetParam()).c_str());
}

// Send continuous pipelined requests while not reading responses, to check
// HTTP/1.1 response flood protection.
TEST_P(IntegrationTest, TestFlood) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(true);
      });
  initialize();

  // Set up a raw connection to easily send requests without reading responses.
  Network::ClientConnectionPtr raw_connection = makeClientConnection(lookupPort("http"));
  raw_connection->connect();

  // Read disable so responses will queue up.
  uint32_t bytes_to_send = 0;
  raw_connection->readDisable(true);
  // Track locally queued bytes, to make sure the outbound client queue doesn't back up.
  raw_connection->addBytesSentCallback([&](uint64_t bytes) { bytes_to_send -= bytes; });

  // Keep sending requests until flood protection kicks in and kills the connection.
  while (raw_connection->state() == Network::Connection::State::Open) {
    // These requests are missing the host header, so will provoke an internally generated error
    // response from Envoy.
    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n");
    bytes_to_send += buffer.length();
    raw_connection->write(buffer, false);
    // Loop until all bytes are sent.
    while (bytes_to_send > 0 && raw_connection->state() == Network::Connection::State::Open) {
      raw_connection->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Verify the connection was closed due to flood protection.
  EXPECT_EQ(1, test_server_->counter("http1.response_flood")->value());
}

TEST_P(IntegrationTest, TestFloodUpstreamErrors) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(1); });
  autonomous_upstream_ = true;
  initialize();

  // Set an Upstream reply with an invalid content-length, which will be rejected by the Envoy.
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "invalid"}}));
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::move(response_headers));

  // Set up a raw connection to easily send requests without reading responses. Also, set a small
  // TCP receive buffer to speed up connection backup while proxying the response flood.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));
  Network::ClientConnectionPtr raw_connection =
      makeClientConnectionWithOptions(lookupPort("http"), options);
  raw_connection->connect();

  // Read disable so responses will queue up.
  uint32_t bytes_to_send = 0;
  raw_connection->readDisable(true);
  // Track locally queued bytes, to make sure the outbound client queue doesn't back up.
  raw_connection->addBytesSentCallback([&](uint64_t bytes) { bytes_to_send -= bytes; });

  // Keep sending requests until flood protection kicks in and kills the connection.
  while (raw_connection->state() == Network::Connection::State::Open) {
    // The upstream response is invalid, and will trigger an internally generated error response
    // from Envoy.
    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nhost: foo.com\r\n\r\n");
    bytes_to_send += buffer.length();
    raw_connection->write(buffer, false);
    // Loop until all bytes are sent.
    while (bytes_to_send > 0 && raw_connection->state() == Network::Connection::State::Open) {
      raw_connection->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Verify the connection was closed due to flood protection.
  EXPECT_EQ(1, test_server_->counter("http1.response_flood")->value());
}

// Make sure flood protection doesn't kick in with many requests sent serially.
TEST_P(IntegrationTest, TestManyBadRequests) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_error_on_invalid_http_message()->set_value(true);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl bad_request{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}};

  for (int i = 0; i < 1000; ++i) {
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(bad_request);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), HttpStatusIs("400"));
  }
  EXPECT_EQ(0, test_server_->counter("http1.response_flood")->value());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10566
TEST_P(IntegrationTest, TestUpgradeHeaderInResponse) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n"
                                              "connection: upgrade\r\n"
                                              "upgrade: h2\r\n"
                                              "Transfer-encoding: chunked\r\n\r\n"
                                              "b\r\nHello World\r\n0\r\n\r\n",
                                              false));

  response->waitForHeaders();
  EXPECT_EQ(nullptr, response->headers().Upgrade());
  EXPECT_EQ(nullptr, response->headers().Connection());
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("Hello World", response->body());
}

TEST_P(IntegrationTest, ConnectWithNoBody) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, false); });
  initialize();

  // Send the payload early so we can regression test that body data does not
  // get proxied until after the response headers are sent.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("CONNECT host.com:80 HTTP/1.1\r\n\r\npayload", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  EXPECT_TRUE(absl::StartsWith(data, "CONNECT host.com:80 HTTP/1.1"));
  // The payload should not be present as the response headers have not been sent.
  EXPECT_FALSE(absl::StrContains(data, "payload")) << data;
  // No transfer-encoding: chunked or connection: close
  EXPECT_FALSE(absl::StrContains(data, "hunked")) << data;
  EXPECT_FALSE(absl::StrContains(data, "onnection")) << data;

  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n\r\n"));
  tcp_client->waitForData("\r\n\r\n", false);
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 200 OK\r\n")) << tcp_client->data();
  // Make sure the following payload is proxied without chunks or any other modifications.
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\npayload"), &data));

  ASSERT_TRUE(fake_upstream_connection->write("return-payload"));
  tcp_client->waitForData("\r\n\r\nreturn-payload", false);
  EXPECT_FALSE(absl::StrContains(tcp_client->data(), "hunked"));

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(IntegrationTest, ConnectWithChunkedBody) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, false); });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write("CONNECT host.com:80 HTTP/1.1\r\n\r\npayload", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  // No transfer-encoding: chunked or connection: close
  EXPECT_FALSE(absl::StrContains(data, "hunked")) << data;
  EXPECT_FALSE(absl::StrContains(data, "onnection")) << data;
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n"));
  // The response will be rejected because chunked headers are not allowed with CONNECT upgrades.
  // Envoy will send a local reply due to the invalid upstream response.
  tcp_client->waitForDisconnect(false);
  EXPECT_TRUE(absl::StartsWith(tcp_client->data(), "HTTP/1.1 503 Service Unavailable\r\n"));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

// Verifies that a 204 response returns without a body
TEST_P(IntegrationTest, Response204WithBody) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // Create a response with a body
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "204"}}, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("204"));
  // The body should be removed
  EXPECT_EQ(0, response->body().size());
}

TEST_P(IntegrationTest, QuitQuitQuit) {
  initialize();
  test_server_->useAdminInterfaceToQuit(true);
}

} // namespace Envoy

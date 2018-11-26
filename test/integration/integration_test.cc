#include "test/integration/integration_test.h"

#include <string>

#include "envoy/config/accesslog/v2/file.pb.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/protobuf/utility.h"

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
using testing::MatchesRegex;
using testing::Not;

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(IntegrationTest, RouterNotFound) { testRouterNotFound(); }

TEST_P(IntegrationTest, RouterNotFoundBodyNoBuffer) { testRouterNotFoundWithBody(); }

TEST_P(IntegrationTest, RouterClusterNotFound404) { testRouterClusterNotFound404(); }

TEST_P(IntegrationTest, RouterClusterNotFound503) { testRouterClusterNotFound503(); }

TEST_P(IntegrationTest, RouterRedirect) { testRouterRedirect(); }

TEST_P(IntegrationTest, RouterDirectResponse) { testRouterDirectResponse(); }

TEST_P(IntegrationTest, ComputedHealthCheck) { testComputedHealthCheck(); }

TEST_P(IntegrationTest, AddEncodedTrailers) { testAddEncodedTrailers(); }

TEST_P(IntegrationTest, DrainClose) { testDrainClose(); }

TEST_P(IntegrationTest, ConnectionClose) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/healthcheck"},
                                                                   {":authority", "host"},
                                                                   {"connection", "close"}});
  response->waitForEndStream();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(IntegrationTest, ShutdownWithActiveConnPoolConnections) {
  testRouterHeaderOnlyRequestAndResponse(false);
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
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

TEST_P(IntegrationTest, Retry) { testRetry(); }

TEST_P(IntegrationTest, RetryAttemptCount) { testRetryAttemptCountHeader(); }

TEST_P(IntegrationTest, RetryHostPredicateFilter) { testRetryHostPredicateFilter(); }

TEST_P(IntegrationTest, RetryPriority) { testRetryPriority(); }

TEST_P(IntegrationTest, EnvoyHandling100Continue) { testEnvoyHandling100Continue(); }

TEST_P(IntegrationTest, EnvoyHandlingDuplicate100Continues) { testEnvoyHandling100Continue(true); }

TEST_P(IntegrationTest, EnvoyProxyingEarly100Continue) { testEnvoyProxying100Continue(true); }

TEST_P(IntegrationTest, EnvoyProxyingLate100Continue) { testEnvoyProxying100Continue(false); }

TEST_P(IntegrationTest, EnvoyProxyingEarly100ContinueWithEncoderFilter) {
  testEnvoyProxying100Continue(true, true);
}

TEST_P(IntegrationTest, EnvoyProxyingLate100ContinueWithEncoderFilter) {
  testEnvoyProxying100Continue(false, true);
}

TEST_P(IntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(IntegrationTest, TwoRequestsWithForcedBackup) { testTwoRequests(true); }

TEST_P(IntegrationTest, UpstreamDisconnectWithTwoRequests) {
  testUpstreamDisconnectWithTwoRequests();
}

TEST_P(IntegrationTest, EncodingHeaderOnlyResponse) { testHeadersOnlyFilterEncoding(); }

TEST_P(IntegrationTest, DecodingHeaderOnlyResponse) { testHeadersOnlyFilterDecoding(); }

TEST_P(IntegrationTest, EncodingHeaderOnlyResponseIntermediateFilters) {
  testHeadersOnlyFilterEncodingIntermediateFilters();
}

TEST_P(IntegrationTest, DecodingHeaderOnlyResponseIntermediateFilters) {
  testHeadersOnlyFilterDecodingIntermediateFilters();
}

TEST_P(IntegrationTest, DecodingHeaderOnlyInterleaved) { testHeadersOnlyFilterInterleaved(); }

TEST_P(IntegrationTest, RetryHittingBufferLimit) { testRetryHittingBufferLimit(); }

TEST_P(IntegrationTest, HittingDecoderFilterLimit) { testHittingDecoderFilterLimit(); }

// Tests idle timeout behaviour with single request and validates that idle timer kicks in
// after given timeout.
TEST_P(IntegrationTest, IdleTimoutBasic) { testIdleTimeoutBasic(); }

// Tests idle timeout behaviour with multiple requests and validates that idle timer kicks in
// after both the requests are done.
TEST_P(IntegrationTest, IdleTimeoutWithTwoRequests) { testIdleTimeoutWithTwoRequests(); }

// Test hitting the bridge filter with too many response bytes to buffer. Given
// the headers are not proxied, the connection manager will send a local error reply.
TEST_P(IntegrationTest, HittingGrpcFilterLimitBufferingHeaders) {
  config_helper_.addFilter("{ name: envoy.grpc_http1_bridge, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"content-type", "application/grpc"},
                              {"x-envoy-retry-grpc-on", "cancelled"}});
  waitForNextUpstreamRequest();

  // Send the overly large response. Because the grpc_http1_bridge filter buffers and buffer
  // limits are exceeded, this will be translated into an unknown gRPC error.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Headers::get().GrpcStatus, "2")); // Unknown gRPC error
}

TEST_P(IntegrationTest, HittingEncoderFilterLimit) { testHittingEncoderFilterLimit(); }

TEST_P(IntegrationTest, BadFirstline) { testBadFirstline(); }

TEST_P(IntegrationTest, MissingDelimiter) { testMissingDelimiter(); }

TEST_P(IntegrationTest, InvalidCharacterInFirstline) { testInvalidCharacterInFirstline(); }

TEST_P(IntegrationTest, InvalidVersion) { testInvalidVersion(); }

TEST_P(IntegrationTest, Http10Disabled) { testHttp10Disabled(); }

TEST_P(IntegrationTest, Http09Enabled) { testHttp09Enabled(); }

TEST_P(IntegrationTest, Http10Enabled) { testHttp10Enabled(); }

TEST_P(IntegrationTest, TestInlineHeaders) { testInlineHeaders(); }

TEST_P(IntegrationTest, Http10WithHostandKeepAlive) { testHttp10WithHostAndKeepAlive(); }

TEST_P(IntegrationTest, NoHost) { testNoHost(); }

TEST_P(IntegrationTest, BadPath) { testBadPath(); }

TEST_P(IntegrationTest, AbsolutePath) { testAbsolutePath(); }

TEST_P(IntegrationTest, AbsolutePathWithPort) { testAbsolutePathWithPort(); }

TEST_P(IntegrationTest, AbsolutePathWithoutPort) { testAbsolutePathWithoutPort(); }

TEST_P(IntegrationTest, Connect) { testConnect(); }

TEST_P(IntegrationTest, ValidZeroLengthContent) { testValidZeroLengthContent(); }

TEST_P(IntegrationTest, InvalidContentLength) { testInvalidContentLength(); }

TEST_P(IntegrationTest, MultipleContentLengths) { testMultipleContentLengths(); }

TEST_P(IntegrationTest, OverlyLongHeaders) { testOverlyLongHeaders(); }

TEST_P(IntegrationTest, UpstreamProtocolError) { testUpstreamProtocolError(); }

TEST_P(IntegrationTest, TestHead) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl head_request{{":method", "HEAD"},
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
  Http::TestHeaderMapImpl content_length_response{{":status", "200"}, {"content-length", "12"}};
  response = sendRequestAndWaitForResponse(head_request, 0, content_length_response, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().ContentLength, "12"));
  EXPECT_EQ(response->headers().TransferEncoding(), nullptr);
  EXPECT_EQ(0, response->body().size());

  cleanupUpstreamAndDownstream();
}

// The Envoy HTTP/1.1 codec ASSERTs that T-E headers are cleared in
// encodeHeaders, so to test upstreams explicitly sending T-E: chunked we have
// to send raw HTTP.
TEST_P(IntegrationTest, TestHeadWithExplicitTE) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  tcp_client->write("HEAD / HTTP/1.1\r\nHost: host\r\n\r\n");
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
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
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
      Http::TestHeaderMapImpl{{":method", "GET"},
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
      [via](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.set_via(via);
      };
}

// Validate in a basic header-only request we get via header insertion.
TEST_P(IntegrationTest, ViaAppendHeaderOnly) {
  config_helper_.addConfigModifier(setVia("bar"));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":authority", "host"},
                                                                   {"via", "foo"},
                                                                   {"connection", "close"}});
  waitForNextUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Headers::get().Via, "foo, bar"));
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();
  codec_client_->waitForDisconnect();
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), HttpStatusIs("200"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().Via, "bar"));
}

// Validate that 100-continue works as expected with via header addition on both request and
// response path.
TEST_P(IntegrationTest, ViaAppendWith100Continue) {
  config_helper_.addConfigModifier(setVia("foo"));
}

// Test delayed close semantics for downstream HTTP/1.1 connections. When an early response is
// sent by Envoy, it will wait for response acknowledgment (via FIN/RST) from the client before
// closing the socket (with a timeout for ensuring cleanup).
TEST_P(IntegrationTest, TestDelayedConnectionTeardownOnGracefulClose) {
  // This test will trigger an early 413 Payload Too Large response due to buffer limits being
  // exceeded. The following filter is needed since the router filter will never trigger a 413.
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("413", response->headers().Status()->value().c_str());
  // With no delayed close processing, Envoy will close the connection immediately after flushing
  // and this should instead return true.
  EXPECT_FALSE(codec_client_->waitForDisconnect(std::chrono::milliseconds(500)));

  // Issue a local close and check that the client did not pick up a remote close which can happen
  // when delayed close semantics are disabled.
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(codec_client_->last_connection_event(), Network::ConnectionEvent::LocalClose);
}

// Test configuration of the delayed close timeout on downstream HTTP/1.1 connections. A value of 0
// disables delayed close processing.
TEST_P(IntegrationTest, TestDelayedConnectionTeardownConfig) {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.mutable_delayed_close_timeout()->set_seconds(0);
      });
  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
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
  EXPECT_EQ(codec_client_->last_connection_event(), Network::ConnectionEvent::RemoteClose);
}

// Test that delay closed connections are eventually force closed when the timeout triggers.
TEST_P(IntegrationTest, TestDelayedConnectionTeardownTimeoutTrigger) {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        // 200ms.
        hcm.mutable_delayed_close_timeout()->set_nanos(200000000);
      });

  initialize();

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  // The delayed close timeout should trigger since client is not closing the connection.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(2000)));
  EXPECT_EQ(codec_client_->last_connection_event(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

} // namespace Envoy

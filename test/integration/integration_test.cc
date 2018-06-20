#include "test/integration/integration_test.h"

#include <string>

#include "envoy/config/accesslog/v2/file.pb.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::MatchesRegex;

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
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
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

TEST_P(IntegrationTest, UpstreamDisconnectWithTwoRequests) {
  testUpstreamDisconnectWithTwoRequests();
}

TEST_P(IntegrationTest, RetryHittingBufferLimit) { testRetryHittingBufferLimit(); }

TEST_P(IntegrationTest, HittingDecoderFilterLimit) { testHittingDecoderFilterLimit(); }

// Tests idle timeout behaviour with single request and validates that idle timer kicks in
// after given timeout.
TEST_P(IntegrationTest, IdleTimoutBasic) { testIdleTimeoutBasic(); }

// Tests idle timeout behaviour with multiple requests and validates that idle timer kicks in
// after both the requests are done.
TEST_P(IntegrationTest, IdleTimeoutWithTwoRequests) { testIdleTimeoutWithTwoRequests(); }

// Test hitting the bridge filter with too many response bytes to buffer. Given
// the headers are not proxied, the connection manager will send a 500.
TEST_P(IntegrationTest, HittingEncoderFilterLimitBufferingHeaders) {
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
  // limits are exceeded, this will be translated into a 500 from Envoy.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024 * 65, false);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_NE(response->headers().GrpcStatus(), nullptr);
  EXPECT_STREQ("2", response->headers().GrpcStatus()->value().c_str()); // Unknown gRPC error
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
  // Request 1.

  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         1024);

  fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  std::string address =
      fake_upstream_connection_->connection().remoteAddress()->ip()->addressAsString();
  EXPECT_EQ(address, address_string);
  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  upstream_request_->waitForEndStream(*dispatcher_);
  // Cleanup both downstream and upstream
  codec_client_->close();
  fake_upstream_connection_->close();
  fake_upstream_connection_->waitForDisconnect();
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
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
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
  EXPECT_STREQ("foo, bar",
               upstream_request_->headers().get(Http::Headers::get().Via)->value().c_str());
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();
  codec_client_->waitForDisconnect();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("bar", response->headers().Via()->value().c_str());
}

// Validate that 100-continue works as expected with via header addition on both request and
// response path.
TEST_P(IntegrationTest, ViaAppendWith100Continue) {
  config_helper_.addConfigModifier(setVia("foo"));
}

} // namespace Envoy

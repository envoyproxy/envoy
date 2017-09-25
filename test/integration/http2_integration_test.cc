#include "test/integration/http2_integration_test.h"

#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, Http2IntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(Http2IntegrationTest, RouterNotFound) { testRouterNotFound(); }

TEST_P(Http2IntegrationTest, RouterNotFoundBodyNoBuffer) { testRouterNotFoundWithBody(); }

TEST_P(Http2IntegrationTest, RouterNotFoundBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterNotFoundWithBody();
}

TEST_P(Http2IntegrationTest, RouterRedirect) { testRouterRedirect(); }

TEST_P(Http2IntegrationTest, ValidZeroLengthContent) { testValidZeroLengthContent(); }

TEST_P(Http2IntegrationTest, InvalidContentLength) { testInvalidContentLength(); }

TEST_P(Http2IntegrationTest, MultipleContentLengths) { testMultipleContentLengths(); }

TEST_P(Http2IntegrationTest, DrainClose) { testDrainClose(); }

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false);
}

TEST_P(Http2IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false);
}

TEST_P(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(Http2IntegrationTest, ShutdownWithActiveConnPoolConnections) {
  testRouterHeaderOnlyRequestAndResponse(false);
}

TEST_P(Http2IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(Http2IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(Http2IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(Http2IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(Http2IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(Http2IntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(Http2IntegrationTest, Retry) { testRetry(); }

TEST_P(Http2IntegrationTest, RetryHittingBufferLimit) { testRetryHittingBufferLimit(); }

TEST_P(Http2IntegrationTest, HittingDecoderFilterLimit) { testHittingDecoderFilterLimit(); }

TEST_P(Http2IntegrationTest, HittingEncoderFilterLimit) { testHittingEncoderFilterLimit(); }

TEST_P(Http2IntegrationTest, GrpcRetry) { testGrpcRetry(); }

// Send a request with overly large headers, and ensure it results in stream reset.
TEST_P(Http2IntegrationTest, MaxHeadersInCodec) {
  Http::TestHeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addCopy("big", std::string(63 * 1024, 'a'));

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  codec_client_->startRequest(big_headers, *response_);
  response_->waitForReset();
  codec_client_->close();
}

TEST_P(Http2IntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

TEST_P(Http2IntegrationTest, BadMagic) {
  initialize();
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
      },
      version_);

  connection.run();
  EXPECT_EQ("", response);
}

TEST_P(Http2IntegrationTest, BadFrame) {
  initialize();
  Buffer::OwnedImpl buffer("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("SETTINGS expected") != std::string::npos);
}

// Send client headers, a GoAway and then a body and ensure the full request and
// response are received.
TEST_P(Http2IntegrationTest, GoAway) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  request_encoder_ = &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/healthcheck"},
                                                                          {":scheme", "http"},
                                                                          {":authority", "host"}},
                                                  *response_);
  codec_client_->goAway();
  codec_client_->sendData(*request_encoder_, 0, true);
  response_->waitForEndStream();
  codec_client_->close();

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
}

TEST_P(Http2IntegrationTest, Trailers) { testTrailers(1024, 2048); }

TEST_P(Http2IntegrationTest, TrailersGiantBody) { testTrailers(1024 * 1024, 1024 * 1024); }

// Interleave two requests and responses and make sure the HTTP2 stack handles this correctly.
void Http2IntegrationTest::simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes) {
  FakeHttpConnectionPtr fake_upstream_connection1;
  FakeHttpConnectionPtr fake_upstream_connection2;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  IntegrationStreamDecoderPtr response1(new IntegrationStreamDecoder(*dispatcher_));
  IntegrationStreamDecoderPtr response2(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start request 1
  encoder1 = &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response1);

  fake_upstream_connection1 = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  upstream_request1 = fake_upstream_connection1->waitForNewStream();

  // Start request 2
  response2.reset(new IntegrationStreamDecoder(*dispatcher_));
  encoder2 = &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response2);
  fake_upstream_connection2 = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  upstream_request2 = fake_upstream_connection2->waitForNewStream();

  // Finish request 1
  codec_client_->sendData(*encoder1, request1_bytes, true);
  upstream_request1->waitForEndStream(*dispatcher_);

  // Finish request 2
  codec_client_->sendData(*encoder2, request2_bytes, true);
  upstream_request2->waitForEndStream(*dispatcher_);

  // Respond to request 2
  upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(request2_bytes, true);
  response2->waitForEndStream();
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_STREQ("200", response2->headers().Status()->value().c_str());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(request2_bytes, true);
  response1->waitForEndStream();
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_STREQ("200", response1->headers().Status()->value().c_str());
  EXPECT_EQ(request2_bytes, response1->body().size());

  // Cleanup both downstream and upstream
  codec_client_->close();
  fake_upstream_connection1->close();
  fake_upstream_connection1->waitForDisconnect();
  fake_upstream_connection2->close();
  fake_upstream_connection2->waitForDisconnect();
}

TEST_P(Http2IntegrationTest, SimultaneousRequest) { simultaneousRequest(1024, 512); }

TEST_P(Http2IntegrationTest, SimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 32, 1024 * 16);
}

} // namespace Envoy

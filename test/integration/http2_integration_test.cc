#include "test/integration/http2_integration_test.h"

#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::HasSubstr;
using ::testing::MatchesRegex;

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(Http2IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false);
}

TEST_P(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
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

TEST_P(Http2IntegrationTest, Retry) { testRetry(); }

TEST_P(Http2IntegrationTest, RetryAttemptCount) { testRetryAttemptCountHeader(); }

static std::string response_metadata_filter = R"EOF(
name: response-metadata-filter
config: {}
)EOF";

// Verifies metadata can be sent at different locations of the responses.
TEST_P(Http2MetadataIntegrationTest, ProxyMetadataInResponse) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends the first request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata before response header.
  const std::string key = "key";
  std::string value = std::string(80 * 1024, '1');
  Http::MetadataMap metadata_map = {{key, value}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(12, true);

  // Verifies metadata is received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find(key)->second, value);

  // Sends the second request.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata after response header followed by an empty data frame with end_stream true.
  value = std::string(10, '2');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  metadata_map = {{key, value}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.erase(metadata_map_vector.begin());
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(0, true);

  // Verifies metadata is received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find(key)->second, value);

  // Sends the third request.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata after response header and before data.
  value = std::string(10, '3');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  metadata_map = {{key, value}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.erase(metadata_map_vector.begin());
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(10, true);

  // Verifies metadata is received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find(key)->second, value);

  // Sends the fourth request.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata between data frames.
  value = std::string(10, '4');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  metadata_map = {{key, value}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.erase(metadata_map_vector.begin());
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(10, true);

  // Verifies metadata is received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find(key)->second, value);

  // Sends the fifth request.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata after the last non-empty data frames.
  value = std::string(10, '5');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  metadata_map = {{key, value}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.erase(metadata_map_vector.begin());
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(0, true);

  // Verifies metadata is received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find(key)->second, value);

  // Sends the sixth request.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends metadata before reset.
  value = std::string(10, '6');
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  metadata_map = {{key, value}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.erase(metadata_map_vector.begin());
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeResetStream();

  // Verifies stream is reset.
  response->waitForReset();
  ASSERT_FALSE(response->complete());
}

TEST_P(Http2MetadataIntegrationTest, ProxyMultipleMetadata) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  const int size = 4;
  std::vector<Http::MetadataMapVector> multiple_vecs(size);
  for (int i = 0; i < size; i++) {
    Runtime::RandomGeneratorImpl random;
    int value_size = random.random() % Http::METADATA_MAX_PAYLOAD_SIZE + 1;
    Http::MetadataMap metadata_map = {{std::string(i, 'a'), std::string(value_size, 'b')}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    multiple_vecs[i].push_back(std::move(metadata_map_ptr));
  }
  upstream_request_->encodeMetadata(multiple_vecs[0]);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeMetadata(multiple_vecs[1]);
  upstream_request_->encodeData(12, false);
  upstream_request_->encodeMetadata(multiple_vecs[2]);
  upstream_request_->encodeData(12, false);
  upstream_request_->encodeMetadata(multiple_vecs[3]);
  upstream_request_->encodeData(12, true);

  // Verifies multiple metadata are received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  for (int i = 0; i < size; i++) {
    for (const auto& metadata : *multiple_vecs[i][0]) {
      EXPECT_EQ(response->metadata_map().find(metadata.first)->second, metadata.second);
    }
  }
  EXPECT_EQ(response->metadata_map().size(), multiple_vecs.size());
}

TEST_P(Http2MetadataIntegrationTest, ProxyInvalidMetadata) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends over-sized metadata before response header.
  const std::string key = "key";
  std::string value = std::string(1024 * 1024, 'a');
  Http::MetadataMap metadata_map = {{key, value}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(12, false);
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeData(12, true);

  // Verifies metadata is not received by the client.
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().size(), 0);
}

void verifyExpectedMetadata(Http::MetadataMap metadata_map, std::set<std::string> keys) {
  for (const auto& key : keys) {
    // keys are the same as their corresponding values.
    EXPECT_EQ(metadata_map.find(key)->second, key);
  }
  EXPECT_EQ(metadata_map.size(), keys.size());
}

TEST_P(Http2MetadataIntegrationTest, TestResponseMetadata) {

  addFilters({response_metadata_filter});
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders(): "headers", "duplicate" and "keep".
  std::set<std::string> expected_metadata_keys = {"headers", "duplicate", "keep"};
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);

  // Upstream responds with headers and data.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders(): "headers" and "duplicate" and metadata added in
  // encodeData(): "data" and "duplicate" are received by the client. Note that "remove" is
  // consumed.
  expected_metadata_keys.insert("data");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);
  EXPECT_EQ(response->keyCount("keep"), 2);

  // Upstream responds with headers, data and trailers.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  Http::TestHeaderMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders(): "headers" and "duplicate", and metadata added in
  // encodeData(): "data" and "duplicate", and metadata added in encodeTrailer(): "trailers" and
  // "duplicate" are received by the client. Note that "remove" is consumed.
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 3);
  EXPECT_EQ(response->keyCount("keep"), 4);

  // Upstream responds with headers, 100-continue and data.
  response = codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/dynamo/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"},
                                                                        {"expect", "100-continue"}},
                                                10);

  waitForNextUpstreamRequest();
  upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});
  response->waitForContinueHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders: "headers" and "duplicate", and metadata added in
  // encodeData(): "data" and "duplicate", and metadata added in encode100Continue(): "100-continue"
  // and "duplicate" are received by the client. Note that "remove" is consumed.
  expected_metadata_keys.erase("trailers");
  expected_metadata_keys.insert("100-continue");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 4);
  EXPECT_EQ(response->keyCount("keep"), 4);

  // Upstream responds with headers and metadata that will not be consumed.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  Http::MetadataMap metadata_map = {{"aaa", "aaa"}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders(): "headers" and "duplicate", and metadata added in
  // encodeMetadata(): "aaa", "keep" and "duplicate" are received by the client. Note that "remove"
  // is consumed.
  expected_metadata_keys.erase("data");
  expected_metadata_keys.erase("100-continue");
  expected_metadata_keys.insert("aaa");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("keep"), 2);

  // Upstream responds with headers, data and metadata that will be consumed.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  metadata_map = {{"consume", "consume"}, {"remove", "remove"}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.clear();
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verify metadata added in encodeHeaders(): "headers" and "duplicate", and metadata added in
  // encodeData(): "data", "duplicate", and metadata added in encodeMetadata(): "keep", "duplicate",
  // "replace" are received by the client. Note that key "remove" and "consume" are consumed.
  expected_metadata_keys.erase("aaa");
  expected_metadata_keys.insert("data");
  expected_metadata_keys.insert("replace");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);
  EXPECT_EQ(response->keyCount("keep"), 3);
}

TEST_P(Http2MetadataIntegrationTest, ProxyMultipleMetadataReachSizeLimit) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Sends multiple metadata after response header until max size limit is reached.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  const int size = 200;
  std::vector<Http::MetadataMapVector> multiple_vecs(size);
  for (int i = 0; i < size; i++) {
    Http::MetadataMap metadata_map = {{"key", std::string(10000, 'a')}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    multiple_vecs[i].push_back(std::move(metadata_map_ptr));
    upstream_request_->encodeMetadata(multiple_vecs[i]);
  }
  upstream_request_->encodeData(12, true);

  // Verifies reset is received.
  response->waitForReset();
  ASSERT_FALSE(response->complete());
}

TEST_P(Http2IntegrationTest, GrpcRouterNotFound) {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/service/notfound", "", downstream_protocol_, version_, "host",
      Http::Headers::get().ContentTypeValues.Grpc);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("12", response->headers().GrpcStatus()->value().getStringView());
}

TEST_P(Http2IntegrationTest, GrpcRetry) { testGrpcRetry(); }

// Verify the case where there is an HTTP/2 codec/protocol error with an active stream.
TEST_P(Http2IntegrationTest, CodecErrorAfterStreamStart) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Send bogus raw data on the connection.
  Buffer::OwnedImpl bogus_data("some really bogus data");
  codec_client_->rawConnection().write(bogus_data, false);

  // Verifies reset is received.
  response->waitForReset();
}

TEST_P(Http2IntegrationTest, BadMagic) {
  initialize();
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
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
        response.append(data.toString());
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
  auto encoder_decoder = codec_client_->startRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->goAway();
  codec_client_->sendData(*request_encoder_, 0, true);
  response->waitForEndStream();
  codec_client_->close();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(Http2IntegrationTest, Trailers) { testTrailers(1024, 2048); }

TEST_P(Http2IntegrationTest, TrailersGiantBody) { testTrailers(1024 * 1024, 1024 * 1024); }

TEST_P(Http2IntegrationTest, GrpcRequestTimeout) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        route->mutable_route()->mutable_max_grpc_timeout()->set_seconds(60 * 60);
      });
  initialize();

  // Envoy will close some number of connections when request times out.
  // Make sure they don't cause assertion failures when we ignore them.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // With upstream request timeout Envoy should send a gRPC-Status "DEADLINE EXCEEDED".
  // TODO: Properly map request timeout to "DEADLINE EXCEEDED" instead of "SERVICE UNAVAILABLE".
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"te", "trailers"},
                              {"grpc-timeout", "1S"}, // 1 Second
                              {"content-type", "application/grpc"}});
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_NE(response->headers().GrpcStatus(), nullptr);
  EXPECT_EQ("14", response->headers().GrpcStatus()->value().getStringView()); // Service Unavailable
  EXPECT_LT(0, test_server_->counter("cluster.cluster_0.upstream_rq_timeout")->value());
}

// Interleave two requests and responses and make sure that idle timeout is handled correctly.
TEST_P(Http2IntegrationTest, IdleTimeoutWithSimultaneousRequests) {
  FakeHttpConnectionPtr fake_upstream_connection1;
  FakeHttpConnectionPtr fake_upstream_connection2;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  int32_t request1_bytes = 1024;
  int32_t request2_bytes = 512;

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
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

  // Start request 1
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  encoder1 = &encoder_decoder.first;
  auto response1 = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  encoder2 = &encoder_decoder2.first;
  auto response2 = std::move(encoder_decoder2.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));

  // Finish request 1
  codec_client_->sendData(*encoder1, request1_bytes, true);
  ASSERT_TRUE(upstream_request1->waitForEndStream(*dispatcher_));

  // Finish request i2
  codec_client_->sendData(*encoder2, request2_bytes, true);
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Respond to request 2
  upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(request2_bytes, true);
  response2->waitForEndStream();
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().Status()->value().getStringView());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Validate that idle time is not kicked in.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_idle_timeout")->value());
  EXPECT_NE(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(request1_bytes, true);
  response1->waitForEndStream();
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().Status()->value().getStringView());
  EXPECT_EQ(request1_bytes, response1->body().size());

  // Do not send any requests and validate idle timeout kicks in after both the requests are done.
  ASSERT_TRUE(fake_upstream_connection1->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 2);
}

// Test request mirroring / shadowing with an HTTP/2 downstream and a request with a body.
TEST_P(Http2IntegrationTest, RequestMirrorWithBody) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_request_mirror_policy()
            ->set_cluster("cluster_0");
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with body.
  IntegrationStreamDecoderPtr request =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         "hello");

  // Wait for the first request as well as the shadow.
  waitForNextUpstreamRequest();

  FakeHttpConnectionPtr fake_upstream_connection2;
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Make sure both requests have a body. Also check the shadow for the shadow headers.
  EXPECT_EQ("hello", upstream_request_->body().toString());
  EXPECT_EQ("hello", upstream_request2->body().toString());
  EXPECT_EQ("host-shadow", upstream_request2->headers().Host()->value().getStringView());

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  request->waitForEndStream();
  EXPECT_EQ("200", request->headers().Status()->value().getStringView());

  // Cleanup.
  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
}

// Interleave two requests and responses and make sure the HTTP2 stack handles this correctly.
void Http2IntegrationTest::simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes) {
  FakeHttpConnectionPtr fake_upstream_connection1;
  FakeHttpConnectionPtr fake_upstream_connection2;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start request 1
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  encoder1 = &encoder_decoder.first;
  auto response1 = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  encoder2 = &encoder_decoder2.first;
  auto response2 = std::move(encoder_decoder2.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));

  // Finish request 1
  codec_client_->sendData(*encoder1, request1_bytes, true);
  ASSERT_TRUE(upstream_request1->waitForEndStream(*dispatcher_));

  // Finish request 2
  codec_client_->sendData(*encoder2, request2_bytes, true);
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Respond to request 2
  upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(request2_bytes, true);
  response2->waitForEndStream();
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().Status()->value().getStringView());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(request2_bytes, true);
  response1->waitForEndStream();
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().Status()->value().getStringView());
  EXPECT_EQ(request2_bytes, response1->body().size());

  // Cleanup both downstream and upstream
  ASSERT_TRUE(fake_upstream_connection1->close());
  ASSERT_TRUE(fake_upstream_connection1->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  codec_client_->close();
}

TEST_P(Http2IntegrationTest, SimultaneousRequest) { simultaneousRequest(1024, 512); }

TEST_P(Http2IntegrationTest, SimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 32, 1024 * 16);
}

// Test downstream connection delayed close processing.
TEST_P(Http2IntegrationTest, DelayedCloseAfterBadFrame) {
  initialize();
  Buffer::OwnedImpl buffer("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& connection, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        connection.dispatcher().exit();
      },
      version_);

  connection.run();
  EXPECT_THAT(response, HasSubstr("SETTINGS expected"));
  // Due to the multiple dispatchers involved (one for the RawConnectionDriver and another for the
  // Envoy server), it's possible the delayed close timer could fire and close the server socket
  // prior to the data callback above firing. Therefore, we may either still be connected, or have
  // received a remote close.
  if (connection.last_connection_event() == Network::ConnectionEvent::Connected) {
    connection.run();
  }
  EXPECT_EQ(connection.last_connection_event(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

// Test disablement of delayed close processing on downstream connections.
TEST_P(Http2IntegrationTest, DelayedCloseDisabled) {
  config_helper_.addConfigModifier(
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.mutable_delayed_close_timeout()->set_seconds(0);
      });
  initialize();
  Buffer::OwnedImpl buffer("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& connection, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        connection.dispatcher().exit();
      },
      version_);

  connection.run();
  EXPECT_THAT(response, HasSubstr("SETTINGS expected"));
  // Due to the multiple dispatchers involved (one for the RawConnectionDriver and another for the
  // Envoy server), it's possible for the 'connection' to receive the data and exit the dispatcher
  // prior to the FIN being received from the server.
  if (connection.last_connection_event() == Network::ConnectionEvent::Connected) {
    connection.run();
  }
  EXPECT_EQ(connection.last_connection_event(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            0);
}

TEST_P(Http2IntegrationTest, PauseAndResume) {
  config_helper_.addFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  config: {}
  )EOF");
  initialize();

  // Send a request with a bit of data, to trigger the filter pausing.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 1, false);

  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Now send the final data frame and make sure it gets proxied.
  codec_client_->sendData(*request_encoder_, 0, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

Http2RingHashIntegrationTest::Http2RingHashIntegrationTest() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->clear_hosts();
    cluster->set_lb_policy(envoy::api::v2::Cluster_LbPolicy_RING_HASH);
    for (int i = 0; i < num_upstreams_; i++) {
      auto* socket = cluster->add_hosts()->mutable_socket_address();
      socket->set_address(Network::Test::getLoopbackAddressString(version_));
    }
  });
}

Http2RingHashIntegrationTest::~Http2RingHashIntegrationTest() {
  if (codec_client_) {
    codec_client_->close();
    codec_client_ = nullptr;
  }
  for (auto it = fake_upstream_connections_.begin(); it != fake_upstream_connections_.end(); ++it) {
    AssertionResult result = (*it)->close();
    RELEASE_ASSERT(result, result.message());
    result = (*it)->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
}

void Http2RingHashIntegrationTest::createUpstreams() {
  for (int i = 0; i < num_upstreams_; i++) {
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2RingHashIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2MetadataIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void Http2RingHashIntegrationTest::sendMultipleRequests(
    int request_bytes, Http::TestHeaderMapImpl headers,
    std::function<void(IntegrationStreamDecoder&)> cb) {
  TestRandomGenerator rand;
  const uint32_t num_requests = 50;
  std::vector<Http::StreamEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeStreamPtr> upstream_requests;

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (uint32_t i = 0; i < num_requests; ++i) {
    auto encoder_decoder = codec_client_->startRequest(headers);
    encoders.push_back(&encoder_decoder.first);
    responses.push_back(std::move(encoder_decoder.second));
    codec_client_->sendData(*encoders[i], request_bytes, true);
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    FakeHttpConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(FakeUpstream::waitForHttpConnection(*dispatcher_, fake_upstreams_,
                                                    fake_upstream_connection));
    // As data and streams are interwoven, make sure waitForNewStream()
    // ignores incoming data and waits for actual stream establishment.
    upstream_requests.emplace_back();
    ASSERT_TRUE(
        fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_requests.back(), true));
    upstream_requests.back()->setAddServedByHeader(true);
    fake_upstream_connections_.push_back(std::move(fake_upstream_connection));
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(upstream_requests[i]->waitForEndStream(*dispatcher_));
    upstream_requests[i]->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    upstream_requests[i]->encodeData(rand.random() % (1024 * 2), true);
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    responses[i]->waitForEndStream();
    EXPECT_TRUE(upstream_requests[i]->complete());
    EXPECT_EQ(request_bytes, upstream_requests[i]->bodyLength());

    EXPECT_TRUE(responses[i]->complete());
    cb(*responses[i]);
  }
}

TEST_P(Http2RingHashIntegrationTest, CookieRoutingNoCookieNoTtl) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        auto* cookie = hash_policy->mutable_cookie();
        cookie->set_name("foo");
      });

  // This test is non-deterministic, so make it extremely unlikely that not all
  // upstreams get hit.
  num_upstreams_ = 2;
  std::set<std::string> served_by;
  sendMultipleRequests(
      1024,
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"}},
      [&](IntegrationStreamDecoder& response) {
        EXPECT_EQ("200", response.headers().Status()->value().getStringView());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie) == nullptr);
        served_by.insert(std::string(
            response.headers().get(Http::LowerCaseString("x-served-by"))->value().getStringView()));
      });
  EXPECT_EQ(served_by.size(), num_upstreams_);
}

TEST_P(Http2RingHashIntegrationTest, CookieRoutingNoCookieWithNonzeroTtlSet) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        auto* cookie = hash_policy->mutable_cookie();
        cookie->set_name("foo");
        cookie->mutable_ttl()->set_seconds(15);
      });

  std::set<std::string> set_cookies;
  sendMultipleRequests(
      1024,
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"}},
      [&](IntegrationStreamDecoder& response) {
        EXPECT_EQ("200", response.headers().Status()->value().getStringView());
        std::string value(
            response.headers().get(Http::Headers::get().SetCookie)->value().getStringView());
        set_cookies.insert(value);
        EXPECT_THAT(value, MatchesRegex("foo=.*; Max-Age=15; HttpOnly"));
      });
  EXPECT_EQ(set_cookies.size(), 1);
}

TEST_P(Http2RingHashIntegrationTest, CookieRoutingNoCookieWithZeroTtlSet) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        auto* cookie = hash_policy->mutable_cookie();
        cookie->set_name("foo");
        cookie->mutable_ttl();
      });

  std::set<std::string> set_cookies;
  sendMultipleRequests(
      1024,
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"}},
      [&](IntegrationStreamDecoder& response) {
        EXPECT_EQ("200", response.headers().Status()->value().getStringView());
        std::string value(
            response.headers().get(Http::Headers::get().SetCookie)->value().getStringView());
        set_cookies.insert(value);
        EXPECT_THAT(value, MatchesRegex("^foo=.*$"));
      });
  EXPECT_EQ(set_cookies.size(), 1);
}

TEST_P(Http2RingHashIntegrationTest, CookieRoutingWithCookieNoTtl) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        auto* cookie = hash_policy->mutable_cookie();
        cookie->set_name("foo");
      });

  std::set<std::string> served_by;
  sendMultipleRequests(
      1024,
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {"cookie", "foo=bar"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"}},
      [&](IntegrationStreamDecoder& response) {
        EXPECT_EQ("200", response.headers().Status()->value().getStringView());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie) == nullptr);
        served_by.insert(std::string(
            response.headers().get(Http::LowerCaseString("x-served-by"))->value().getStringView()));
      });
  EXPECT_EQ(served_by.size(), 1);
}

TEST_P(Http2RingHashIntegrationTest, CookieRoutingWithCookieWithTtlSet) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        auto* cookie = hash_policy->mutable_cookie();
        cookie->set_name("foo");
        cookie->mutable_ttl()->set_seconds(15);
      });

  std::set<std::string> served_by;
  sendMultipleRequests(
      1024,
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {"cookie", "foo=bar"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"}},
      [&](IntegrationStreamDecoder& response) {
        EXPECT_EQ("200", response.headers().Status()->value().getStringView());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie) == nullptr);
        served_by.insert(std::string(
            response.headers().get(Http::LowerCaseString("x-served-by"))->value().getStringView()));
      });
  EXPECT_EQ(served_by.size(), 1);
}

} // namespace Envoy

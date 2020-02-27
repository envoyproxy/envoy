#include "test/integration/http2_integration_test.h"

#include <algorithm>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

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

TEST_P(Http2IntegrationTest, LargeRequestTrailersRejected) { testLargeRequestTrailers(66, 60); }

static std::string response_metadata_filter = R"EOF(
name: response-metadata-filter
typed_config:
  "@type": type.googleapis.com/google.protobuf.Empty
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  std::set<std::string> expected_metadata_keys = {"headers", "duplicate"};
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);

  // Upstream responds with headers and data.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("data");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);

  // Upstream responds with headers, data and trailers.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 3);

  // Upstream responds with headers, 100-continue and data.
  response =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/dynamo/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"},
                                                                        {"expect", "100-continue"}},
                                         10);

  waitForNextUpstreamRequest();
  upstream_request_->encode100ContinueHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  response->waitForContinueHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.erase("trailers");
  expected_metadata_keys.insert("100-continue");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 4);

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
  expected_metadata_keys.erase("data");
  expected_metadata_keys.erase("100-continue");
  expected_metadata_keys.insert("aaa");
  expected_metadata_keys.insert("keep");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);

  // Upstream responds with headers, data and metadata that will be consumed.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  metadata_map = {{"consume", "consume"}};
  metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  metadata_map_vector.clear();
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.erase("aaa");
  expected_metadata_keys.insert("data");
  expected_metadata_keys.insert("replace");
  verifyExpectedMetadata(response->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);
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

// Verifies small metadata can be sent at different locations of a request.
TEST_P(Http2MetadataIntegrationTest, ProxySmallMetadataInRequest) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Http::MetadataMap metadata_map = {{"key", "value"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  waitForNextUpstreamRequest();

  // Verifies metadata is received by upstream.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  EXPECT_EQ(upstream_request_->metadata_map().find("key")->second, "value");
  EXPECT_EQ(upstream_request_->metadata_map().size(), 1);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("key")->second, 3);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

// Verifies large metadata can be sent at different locations of a request.
TEST_P(Http2MetadataIntegrationTest, ProxyLargeMetadataInRequest) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  std::string value = std::string(80 * 1024, '1');
  Http::MetadataMap metadata_map = {{"key", value}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  waitForNextUpstreamRequest();

  // Verifies metadata is received upstream.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  EXPECT_EQ(upstream_request_->metadata_map().find("key")->second, value);
  EXPECT_EQ(upstream_request_->metadata_map().size(), 1);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("key")->second, 3);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

TEST_P(Http2MetadataIntegrationTest, RequestMetadataReachSizeLimit) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  std::string value = std::string(10 * 1024, '1');
  Http::MetadataMap metadata_map = {{"key", value}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 1, false);
  for (int i = 0; i < 200; i++) {
    codec_client_->sendMetadata(*request_encoder_, metadata_map);
    if (codec_client_->disconnected()) {
      break;
    }
  }

  // Verifies client connection will be closed.
  codec_client_->waitForDisconnect();
  ASSERT_FALSE(response->complete());
}

static std::string request_metadata_filter = R"EOF(
name: request-metadata-filter
typed_config:
  "@type": type.googleapis.com/google.protobuf.Empty
)EOF";

TEST_P(Http2MetadataIntegrationTest, ConsumeAndInsertRequestMetadata) {
  addFilters({request_metadata_filter});
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  // Verifies a headers metadata added.
  std::set<std::string> expected_metadata_keys = {"headers"};
  expected_metadata_keys.insert("metadata");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);

  // Sends a headers only request with metadata. An empty data frame carries end_stream.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  response = std::move(encoder_decoder.second);
  Http::MetadataMap metadata_map = {{"consume", "consume"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 0, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("data");
  expected_metadata_keys.insert("metadata");
  expected_metadata_keys.insert("replace");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("metadata")->second, 3);
  // Verifies zero length data received, and end_stream is true.
  EXPECT_EQ(true, upstream_request_->receivedData());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends headers, data, metadata and trailer.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  codec_client_->sendData(*request_encoder_, 10, false);
  metadata_map = {{"consume", "consume"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("metadata")->second, 4);

  // Sends headers, large data, metadata. Large data triggers decodeData() multiple times, and each
  // time, a "data" metadata is added.
  auto encoder_decoder_3 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_3.first;
  response = std::move(encoder_decoder_3.second);
  codec_client_->sendData(*request_encoder_, 100000, false);
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 100000, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());

  expected_metadata_keys.erase("trailers");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);
  EXPECT_GE(upstream_request_->duplicated_metadata_key_count().find("data")->second, 2);
  EXPECT_GE(upstream_request_->duplicated_metadata_key_count().find("metadata")->second, 3);

  // Sends multiple metadata.
  auto encoder_decoder_4 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_4.first;
  response = std::move(encoder_decoder_4.second);
  metadata_map = {{"metadata1", "metadata1"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 10, false);
  metadata_map = {{"metadata2", "metadata2"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  metadata_map = {{"consume", "consume"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("metadata1");
  expected_metadata_keys.insert("metadata2");
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("metadata")->second, 6);
}

static std::string decode_headers_only = R"EOF(
name: decode-headers-only
typed_config:
  "@type": type.googleapis.com/google.protobuf.Empty
)EOF";

void Http2MetadataIntegrationTest::runHeaderOnlyTest(bool send_request_body, size_t body_size) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with body. Only headers will pass through filters.
  IntegrationStreamDecoderPtr response;
  if (send_request_body) {
    response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "host"}},
        body_size);
  } else {
    response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "host"}});
  }
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

void Http2MetadataIntegrationTest::verifyHeadersOnlyTest() {
  // Verifies a headers metadata added.
  std::set<std::string> expected_metadata_keys = {"headers"};
  expected_metadata_keys.insert("metadata");
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);

  // Verifies zero length data received, and end_stream is true.
  EXPECT_EQ(true, upstream_request_->receivedData());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());
}

TEST_P(Http2MetadataIntegrationTest, DecodingHeadersOnlyRequestWithRequestMetadataEmptyData) {
  addFilters({request_metadata_filter, decode_headers_only});

  // Send a request with body, and body size is 0.
  runHeaderOnlyTest(true, 0);
  verifyHeadersOnlyTest();
}

TEST_P(Http2MetadataIntegrationTest, DecodingHeadersOnlyRequestWithRequestMetadataNoneEmptyData) {
  addFilters({request_metadata_filter, decode_headers_only});
  // Send a request with body, and body size is 128.
  runHeaderOnlyTest(true, 128);
  verifyHeadersOnlyTest();
}

TEST_P(Http2MetadataIntegrationTest, DecodingHeadersOnlyRequestWithRequestMetadataDiffFilterOrder) {
  addFilters({decode_headers_only, request_metadata_filter});
  // Send a request with body, and body size is 128.
  runHeaderOnlyTest(true, 128);
  verifyHeadersOnlyTest();
}

TEST_P(Http2MetadataIntegrationTest, HeadersOnlyRequestWithRequestMetadata) {
  addFilters({request_metadata_filter});
  // Send a headers only request.
  runHeaderOnlyTest(false, 0);
  verifyHeadersOnlyTest();
}

void Http2MetadataIntegrationTest::testRequestMetadataWithStopAllFilter() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends multiple metadata.
  const size_t size = 10;
  default_request_headers_.addCopy("content_size", std::to_string(size));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Http::MetadataMap metadata_map = {{"metadata1", "metadata1"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, size, false);
  metadata_map = {{"metadata2", "metadata2"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  metadata_map = {{"consume", "consume"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  std::set<std::string> expected_metadata_keys = {"headers",   "data",    "metadata", "metadata1",
                                                  "metadata2", "replace", "trailers"};
  verifyExpectedMetadata(upstream_request_->metadata_map(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicated_metadata_key_count().find("metadata")->second, 6);
}

static std::string metadata_stop_all_filter = R"EOF(
name: metadata-stop-all-filter
typed_config:
  "@type": type.googleapis.com/google.protobuf.Empty
)EOF";

TEST_P(Http2MetadataIntegrationTest, RequestMetadataWithStopAllFilterBeforeMetadataFilter) {
  addFilters({request_metadata_filter, metadata_stop_all_filter});
  testRequestMetadataWithStopAllFilter();
}

TEST_P(Http2MetadataIntegrationTest, RequestMetadataWithStopAllFilterAfterMetadataFilter) {
  addFilters({metadata_stop_all_filter, request_metadata_filter});
  testRequestMetadataWithStopAllFilter();
}

TEST_P(Http2MetadataIntegrationTest, TestAddEncodedMetadata) {
  config_helper_.addFilter(R"EOF(
name: encode-headers-return-stop-all-filter
)EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers, data and trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  const int count = 70;
  const int size = 1000;
  const int added_decoded_data_size = 1;

  default_response_headers_.addCopy("content_size", std::to_string(count * size));
  default_response_headers_.addCopy("added_size", std::to_string(added_decoded_data_size));
  default_response_headers_.addCopy("is_first_trigger", "value");

  upstream_request_->encodeHeaders(default_response_headers_, false);
  for (int i = 0; i < count - 1; i++) {
    upstream_request_->encodeData(size, false);
  }

  upstream_request_->encodeData(size, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadata_map().find("headers")->second, "headers");
  EXPECT_EQ(response->metadata_map().find("data")->second, "data");
  EXPECT_EQ(response->metadata_map().find("trailers")->second, "trailers");
  EXPECT_EQ(response->metadata_map().size(), 3);
  EXPECT_EQ(count * size + added_decoded_data_size * 2, response->body().size());
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
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
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

TEST_P(Http2IntegrationTest, Trailers) { testTrailers(1024, 2048, false, false); }

TEST_P(Http2IntegrationTest, TrailersGiantBody) {
  testTrailers(1024 * 1024, 1024 * 1024, false, false);
}

TEST_P(Http2IntegrationTest, GrpcRequestTimeout) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
  Http::RequestEncoder* encoder1;
  Http::RequestEncoder* encoder2;
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  int32_t request1_bytes = 1024;
  int32_t request2_bytes = 512;

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

  // Start request 1
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  encoder1 = &encoder_decoder.first;
  auto response1 = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
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
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_0");
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with body.
  IntegrationStreamDecoderPtr request =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "POST"},
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

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
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
  Http::RequestEncoder* encoder1;
  Http::RequestEncoder* encoder2;
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start request 1
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  encoder1 = &encoder_decoder.first;
  auto response1 = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(request2_bytes, true);
  response2->waitForEndStream();
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().Status()->value().getStringView());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
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
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(0); });
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
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
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
  upstream_request_->encodeHeaders(default_response_headers_, false);

  response->waitForHeaders();
  upstream_request_->encodeData(0, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

TEST_P(Http2IntegrationTest, PauseAndResumeHeadersOnly) {
  config_helper_.addFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
}

Http2RingHashIntegrationTest::Http2RingHashIntegrationTest() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->clear_load_assignment();
    cluster->mutable_load_assignment()->add_endpoints();
    cluster->mutable_load_assignment()->set_cluster_name(cluster->name());
    cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::RING_HASH);
    for (int i = 0; i < num_upstreams_; i++) {
      auto* socket = cluster->mutable_load_assignment()
                         ->mutable_endpoints(0)
                         ->add_lb_endpoints()
                         ->mutable_endpoint()
                         ->mutable_address()
                         ->mutable_socket_address();
      socket->set_address(Network::Test::getLoopbackAddressString(version_));
    }
  });
}

Http2RingHashIntegrationTest::~Http2RingHashIntegrationTest() {
  if (codec_client_) {
    codec_client_->close();
    codec_client_ = nullptr;
  }
  for (auto& fake_upstream_connection : fake_upstream_connections_) {
    AssertionResult result = fake_upstream_connection->close();
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection->waitForDisconnect();
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
    int request_bytes, Http::TestRequestHeaderMapImpl headers,
    std::function<void(IntegrationStreamDecoder&)> cb) {
  TestRandomGenerator rand;
  const uint32_t num_requests = 50;
  std::vector<Http::RequestEncoder*> encoders;
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
    upstream_requests[i]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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

namespace {
const int64_t TransmitThreshold = 100 * 1024 * 1024;
} // namespace

void Http2FloodMitigationTest::setNetworkConnectionBufferSize() {
  // nghttp2 library has its own internal mitigation for outbound control frames (see
  // NGHTTP2_DEFAULT_MAX_OBQ_FLOOD_ITEM). The default nghttp2 mitigation threshold of 1K is modified
  // to 10K in the ConnectionImpl::Http2Options::Http2Options. The mitigation is triggered when
  // there are more than 10000 PING or SETTINGS frames with ACK flag in the nghttp2 internal
  // outbound queue. It is possible to trigger this mitigation in nghttp2 before triggering Envoy's
  // own flood mitigation. This can happen when a buffer large enough to contain over 10K PING or
  // SETTINGS frames is dispatched to the nghttp2 library. To prevent this from happening the
  // network connection receive buffer needs to be smaller than 90Kb (which is 10K SETTINGS frames).
  // Set it to the arbitrarily chosen value of 32K. Note that this buffer has 16K lower bound.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->listeners_size() >= 1, "");
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    listener->mutable_per_connection_buffer_limit_bytes()->set_value(32 * 1024);
  });
}

void Http2FloodMitigationTest::beginSession() {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  // set lower outbound frame limits to make tests run faster
  config_helper_.setOutboundFramesLimits(1000, 100);
  initialize();
  tcp_client_ = makeTcpConnection(lookupPort("http"));
  startHttp2Session();
}

Http2Frame Http2FloodMitigationTest::readFrame() {
  Http2Frame frame;
  tcp_client_->waitForData(frame.HeaderSize);
  frame.setHeader(tcp_client_->data());
  tcp_client_->clearData(frame.HeaderSize);
  auto len = frame.payloadSize();
  if (len) {
    tcp_client_->waitForData(len);
    frame.setPayload(tcp_client_->data());
    tcp_client_->clearData(len);
  }
  return frame;
}

void Http2FloodMitigationTest::sendFame(const Http2Frame& frame) {
  ASSERT_TRUE(tcp_client_->connected());
  tcp_client_->write(std::string(frame), false, false);
}

void Http2FloodMitigationTest::startHttp2Session() {
  tcp_client_->write(Http2Frame::Preamble, false, false);

  // Send empty initial SETTINGS frame.
  auto settings = Http2Frame::makeEmptySettingsFrame();
  tcp_client_->write(std::string(settings), false, false);

  // Read initial SETTINGS frame from the server.
  readFrame();

  // Send an SETTINGS ACK.
  settings = Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack);
  tcp_client_->write(std::string(settings), false, false);

  // read pending SETTINGS and WINDOW_UPDATE frames
  readFrame();
  readFrame();
}

// Verify that the server detects the flood of the given frame.
void Http2FloodMitigationTest::floodServer(const Http2Frame& frame, const std::string& flood_stat) {
  // pack the as many frames as we can into 16k buffer
  const int FrameCount = (16 * 1024) / frame.size();
  std::vector<char> buf(FrameCount * frame.size());
  for (auto pos = buf.begin(); pos != buf.end();) {
    pos = std::copy(frame.begin(), frame.end(), pos);
  }

  tcp_client_->readDisable(true);
  int64_t total_bytes_sent = 0;
  // If the flood protection is not working this loop will keep going
  // forever until it is killed by blaze timer or run out of memory.
  // Add early stop if we have sent more than 100M of frames, as it this
  // point it is obvious something is wrong.
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    tcp_client_->write({buf.begin(), buf.end()}, false, false);
    total_bytes_sent += buf.size();
  }

  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

// Verify that the server detects the flood using specified request parameters.
void Http2FloodMitigationTest::floodServer(absl::string_view host, absl::string_view path,
                                           Http2Frame::ResponseStatus expected_http_status,
                                           const std::string& flood_stat) {
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, host, path);
  sendFame(request);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(expected_http_status, frame.responseStatus());
  tcp_client_->readDisable(true);
  uint64_t total_bytes_sent = 0;
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    request = Http2Frame::makeRequest(++request_idx, host, path);
    sendFame(request);
    total_bytes_sent += request.size();
  }
  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  if (!flood_stat.empty()) {
    EXPECT_EQ(1, test_server_->counter(flood_stat)->value());
  }
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2FloodMitigationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(Http2FloodMitigationTest, Ping) {
  setNetworkConnectionBufferSize();
  beginSession();
  floodServer(Http2Frame::makePingFrame(), "http2.outbound_control_flood");
}

TEST_P(Http2FloodMitigationTest, Settings) {
  setNetworkConnectionBufferSize();
  beginSession();
  floodServer(Http2Frame::makeEmptySettingsFrame(), "http2.outbound_control_flood");
}

// Verify that the server can detect flood of internally generated 404 responses.
TEST_P(Http2FloodMitigationTest, 404) {
  // Change the default route to be restrictive, and send a request to a non existent route.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  beginSession();

  // Send requests to a non existent path to generate 404s
  floodServer("host", "/notfound", Http2Frame::ResponseStatus::NotFound, "http2.outbound_flood");
}

// Verify that the server can detect flood of DATA frames
TEST_P(Http2FloodMitigationTest, Data) {
  // Set large buffer limits so the test is not affected by the flow control.
  config_helper_.setBufferLimits(1024 * 1024 * 1024, 1024 * 1024 * 1024);
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  floodServer("host", "/test/long/url", Http2Frame::ResponseStatus::Ok, "http2.outbound_flood");
}

// Verify that the server can detect flood of RST_STREAM frames.
TEST_P(Http2FloodMitigationTest, RST_STREAM) {
  // Use invalid HTTP headers to trigger sending RST_STREAM frames.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()->set_stream_error_on_invalid_http_messaging(true);
      });
  beginSession();

  int i = 0;
  auto request = Http::Http2::Http2Frame::makeMalformedRequest(i);
  sendFame(request);
  auto response = readFrame();
  // Make sure we've got RST_STREAM from the server
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());
  uint64_t total_bytes_sent = 0;
  while (total_bytes_sent < TransmitThreshold && tcp_client_->connected()) {
    request = Http::Http2::Http2Frame::makeMalformedRequest(++i);
    sendFame(request);
    total_bytes_sent += request.size();
  }
  EXPECT_LE(total_bytes_sent, TransmitThreshold) << "Flood mitigation is broken.";
  EXPECT_EQ(1, test_server_->counter("http2.outbound_control_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

// Verify that the server stop reading downstream connection on protocol error.
TEST_P(Http2FloodMitigationTest, TooManyStreams) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_max_concurrent_streams()->set_value(2);
      });
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Exceed the number of streams allowed by the server. The server should stop reading from the
  // client. Verify that the client was unable to stuff a lot of data into the server.
  floodServer("host", "/test/long/url", Http2Frame::ResponseStatus::Ok, "");
}

TEST_P(Http2FloodMitigationTest, EmptyHeaders) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()
            ->mutable_max_consecutive_inbound_frames_with_empty_payload()
            ->set_value(0);
      });
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeEmptyHeadersFrame(request_idx);
  sendFame(request);

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyHeadersContinuation) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeEmptyHeadersFrame(request_idx);
  sendFame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyContinuationFrame(request_idx);
    sendFame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, EmptyData) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  uint32_t request_idx = 0;
  auto request = Http2Frame::makePostRequest(request_idx, "host", "/");
  sendFame(request);

  for (int i = 0; i < 2; i++) {
    request = Http2Frame::makeEmptyDataFrame(request_idx);
    sendFame(request);
  }

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.inbound_empty_frames_flood")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
}

TEST_P(Http2FloodMitigationTest, PriorityIdleStream) {
  beginSession();

  floodServer(Http2Frame::makePriorityFrame(0, 1), "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, PriorityOpenStream) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);

  floodServer(Http2Frame::makePriorityFrame(request_idx, request_idx + 1),
              "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, PriorityClosedStream) {
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);
  // Reading response marks this stream as closed in nghttp2.
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());

  floodServer(Http2Frame::makePriorityFrame(request_idx, request_idx + 1),
              "http2.inbound_priority_frames_flood");
}

TEST_P(Http2FloodMitigationTest, WindowUpdate) {
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Open stream.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);

  floodServer(Http2Frame::makeWindowUpdateFrame(request_idx, 1),
              "http2.inbound_window_update_frames_flood");
}

// Verify that the HTTP/2 connection is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeader) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  // Send invalid request.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(request_idx, "host", "/");
  sendFame(request);

  tcp_client_->waitForDisconnect();

  EXPECT_EQ(1, test_server_->counter("http2.rx_messaging_error")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.invalid.header.field"));
  // expect a downstream protocol error.
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
}

// Verify that only the offending stream is terminated upon receiving invalid HEADERS frame.
TEST_P(Http2FloodMitigationTest, ZerolenHeaderAllowed) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http2_protocol_options()->set_stream_error_on_invalid_http_messaging(true);
      });
  autonomous_upstream_ = true;
  beginSession();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Send invalid request.
  uint32_t request_idx = 0;
  auto request = Http2Frame::makeMalformedRequestWithZerolenHeader(request_idx, "host", "/");
  sendFame(request);
  // Make sure we've got RST_STREAM from the server.
  auto response = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, response.type());

  // Send valid request using the same connection.
  request_idx++;
  request = Http2Frame::makeRequest(request_idx, "host", "/");
  sendFame(request);
  response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, response.responseStatus());

  tcp_client_->close();

  EXPECT_EQ(1, test_server_->counter("http2.rx_messaging_error")->value());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("http2.invalid.header.field"));
  // expect Downstream Protocol Error
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
}

} // namespace Envoy

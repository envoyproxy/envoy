#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

#include "socket_interface_swap.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#endif

#include "absl/synchronization/mutex.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/socket_option_impl.h"

#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::HasSubstr;
using ::testing::MatchesRegex;

namespace Envoy {
namespace {
std::vector<int> stoiAccessLogString(const std::string& access_log_entry_of_ints) {
  std::vector<int> ret;
  const std::vector<std::string> split_string = TestUtility::split(access_log_entry_of_ints, ' ');
  ret.reserve(split_string.size());

  for (auto& str : split_string) {
    ret.push_back(std::stoi(str));
  }

  return ret;
}

// Helper class that tabulates the bytes of a given stream by consuming the raw HTTP2 frames.
struct StreamByteAccumulator {
  uint32_t stream_wire_bytes_recieved_ = 0;
  uint32_t stream_data_frames_recieved_ = 0;
  uint32_t stream_body_payload_recieved_ = 0;
  uint32_t stream_wire_header_bytes_recieved_ = 0;

  void countFrame(const Http2Frame& frame) {
    stream_wire_bytes_recieved_ += frame.size();
    if (frame.type() == Http2Frame::Type::Data) {
      ++stream_data_frames_recieved_;
      stream_body_payload_recieved_ += frame.payloadSize();
    } else if (frame.type() == Http2Frame::Type::Headers) {
      stream_wire_header_bytes_recieved_ += frame.size();
    }
  }

  int bodyWireBytesReceivedDiscountingHeaders() const {
    return stream_wire_bytes_recieved_ - stream_wire_header_bytes_recieved_;
  }

  int bodyWireBytesReceivedGivenPayloadAndFrames() const {
    return stream_body_payload_recieved_ + stream_data_frames_recieved_ * Http2Frame::HeaderSize;
  }
};

} // end namespace

#define EXCLUDE_DOWNSTREAM_HTTP3                                                                   \
  if (downstreamProtocol() == Http::CodecType::HTTP3) {                                            \
    return;                                                                                        \
  }

class MultiplexedIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes);
};

constexpr uint32_t GiantPayoadSizeByte = 10 * 1024 * 1024;

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiplexedIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2, Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

class MultiplexedIntegrationTestWithSimulatedTime : public Event::TestUsingSimulatedTime,
                                                    public MultiplexedIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiplexedIntegrationTestWithSimulatedTime,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2, Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(MultiplexedIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, false);
}

TEST_P(MultiplexedIntegrationTest, Http3StreamInfoDownstreamHandshakeTiming) {
  if (downstreamProtocol() != Http::CodecType::HTTP3) {
    // See SslIntegrationTest for equivalent tests for HTTP/1 and HTTP/2.
    return;
  }

  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_FALSE(
      response->headers().get(Http::LowerCaseString("downstream_handshake_complete")).empty());
}

TEST_P(MultiplexedIntegrationTest, RouterRequestAndResponseWithGiantBodyNoBuffer) {
  ENVOY_LOG_MISC(warn, "manually lowering logs to error");
  LogLevelSetter save_levels(spdlog::level::err);
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, false);
}

TEST_P(MultiplexedIntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, false);
}

TEST_P(MultiplexedIntegrationTest, LargeFlowControlOnAndGiantBody) {
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  config_helper_.setBufferLimits(128 * 1024,
                                 128 * 1024); // Set buffer limits upstream and downstream.
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, false);
}

TEST_P(MultiplexedIntegrationTest, RouterRequestAndResponseWithBodyAndContentLengthNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false, true);
}

TEST_P(MultiplexedIntegrationTest, RouterRequestAndResponseWithGiantBodyAndContentLengthNoBuffer) {
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, true);
}

TEST_P(MultiplexedIntegrationTest, FlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, true);
}

TEST_P(MultiplexedIntegrationTest, LargeFlowControlOnAndGiantBodyWithContentLength) {
  config_helper_.addConfigModifier(ConfigHelper::adjustUpstreamTimeoutForTsan);
  config_helper_.setBufferLimits(128 * 1024,
                                 128 * 1024); // Set buffer limits upstream and downstream.
  testGiantRequestAndResponse(GiantPayoadSizeByte, GiantPayoadSizeByte, true);
}

TEST_P(MultiplexedIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(MultiplexedIntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(MultiplexedIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(MultiplexedIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(MultiplexedIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(MultiplexedIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(MultiplexedIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(MultiplexedIntegrationTest, Retry) { testRetry(); }

TEST_P(MultiplexedIntegrationTest, RetryAttemptCount) { testRetryAttemptCountHeader(); }

TEST_P(MultiplexedIntegrationTest, LargeRequestTrailersRejected) {
  testLargeRequestTrailers(66, 60);
}

// Verify downstream codec stream flush timeout.
TEST_P(MultiplexedIntegrationTest, CodecStreamIdleTimeout) {
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        constexpr uint64_t IdleTimeoutMs = 400;
        hcm.mutable_stream_idle_timeout()->set_nanos(IdleTimeoutMs * 1000 * 1000);
      });
  initialize();
  const size_t stream_flow_control_window =
      downstream_protocol_ == Http::CodecType::HTTP3 ? 32 * 1024 : 65535;
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions());
  http2_options.mutable_initial_stream_window_size()->set_value(stream_flow_control_window);
#ifdef ENVOY_ENABLE_QUIC
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetInitialStreamFlowControlWindowToSend(stream_flow_control_window);
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetInitialSessionFlowControlWindowToSend(stream_flow_control_window);
  }
#endif
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(stream_flow_control_window + 2000, true);
  std::string flush_timeout_counter(downstreamProtocol() == Http::CodecType::HTTP3
                                        ? "http3.tx_flush_timeout"
                                        : "http2.tx_flush_timeout");
  test_server_->waitForCounterEq(flush_timeout_counter, 1);
  ASSERT_TRUE(response->waitForReset());
}

TEST_P(MultiplexedIntegrationTest, Http2DownstreamKeepalive) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Http3 keepalive doesn't timeout and close connection.
  constexpr uint64_t interval_ms = 1;
  constexpr uint64_t timeout_ms = 250;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()
            ->mutable_connection_keepalive()
            ->mutable_interval()
            ->set_nanos(interval_ms * 1000 * 1000);
        hcm.mutable_http2_protocol_options()
            ->mutable_connection_keepalive()
            ->mutable_timeout()
            ->set_nanos(timeout_ms * 1000 * 1000);
      });
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // This call is NOT running the event loop of the client, so downstream PINGs will
  // not receive a response.
  test_server_->waitForCounterEq("http2.keepalive_timeout", 1,
                                 std::chrono::milliseconds(timeout_ms * 2));

  ASSERT_TRUE(response->waitForReset());
}

static std::string response_metadata_filter = R"EOF(
name: response-metadata-filter
)EOF";

class Http2MetadataIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void SetUp() override {
    HttpProtocolIntegrationTest::SetUp();
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http2_protocol_options()
              ->set_allow_metadata(true);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  }

  void testRequestMetadataWithStopAllFilter();

  void verifyHeadersOnlyTest();

  void runHeaderOnlyTest(bool send_request_body, size_t body_size);

protected:
  // Utility function to prepend filters. Note that the filters
  // are added in reverse order.
  void prependFilters(std::vector<std::string> filters) {
    for (const auto& filter : filters) {
      config_helper_.prependFilter(filter);
    }
  }
};

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find(key)->second, value);
  EXPECT_EQ(1, response->metadataMapsDecodedCount());

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find(key)->second, value);
  EXPECT_EQ(1, response->metadataMapsDecodedCount());

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find(key)->second, value);
  EXPECT_EQ(1, response->metadataMapsDecodedCount());

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find(key)->second, value);
  EXPECT_EQ(1, response->metadataMapsDecodedCount());

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find(key)->second, value);
  EXPECT_EQ(1, response->metadataMapsDecodedCount());

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
  ASSERT_TRUE(response->waitForReset());
  ASSERT_FALSE(response->complete());

  // The cluster should have received the reset.
  // The downstream codec should send one.
  std::string counter =
      absl::StrCat("cluster.cluster_0.", upstreamProtocolStatsRoot(), ".rx_reset");
  test_server_->waitForCounterEq(counter, 1);
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
    Random::RandomGeneratorImpl random;
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(4, response->metadataMapsDecodedCount());
  for (int i = 0; i < size; i++) {
    for (const auto& metadata : *multiple_vecs[i][0]) {
      EXPECT_EQ(response->metadataMap().find(metadata.first)->second, metadata.second);
    }
  }
  EXPECT_EQ(response->metadataMap().size(), multiple_vecs.size());
}

// Disabled temporarily see #19040
#if 0
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(0, response->metadataMapsDecodedCount());
  EXPECT_EQ(response->metadataMap().size(), 0);
}
#endif

void verifyExpectedMetadata(Http::MetadataMap metadata_map, std::set<std::string> keys) {
  for (const auto& key : keys) {
    // keys are the same as their corresponding values.
    EXPECT_EQ(metadata_map.find(key)->second, key);
  }
  EXPECT_EQ(metadata_map.size(), keys.size());
}

TEST_P(Http2MetadataIntegrationTest, TestResponseMetadata) {
  prependFilters({response_metadata_filter});
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  std::set<std::string> expected_metadata_keys = {"headers", "duplicate"};
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);

  // Upstream responds with headers and data.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("data");
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);
  EXPECT_EQ(2, response->metadataMapsDecodedCount());

  // Upstream responds with headers, data and trailers.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(10, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 3);
  EXPECT_EQ(4, response->metadataMapsDecodedCount());

  // Upstream responds with headers, 100-continue and data.
  response =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/dynamo/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"},
                                                                        {"expect", "100-contINUE"}},
                                         10);

  waitForNextUpstreamRequest();
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  response->waitFor1xxHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.erase("trailers");
  expected_metadata_keys.insert("100-continue");
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 4);
  EXPECT_EQ(4, response->metadataMapsDecodedCount());

  // Upstream responds with headers and metadata that will not be consumed.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  Http::MetadataMap metadata_map = {{"aaa", "aaa"}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.erase("data");
  expected_metadata_keys.erase("100-continue");
  expected_metadata_keys.insert("aaa");
  expected_metadata_keys.insert("keep");
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(2, response->metadataMapsDecodedCount());

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

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.erase("aaa");
  expected_metadata_keys.insert("data");
  expected_metadata_keys.insert("replace");
  verifyExpectedMetadata(response->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(response->keyCount("duplicate"), 2);
  EXPECT_EQ(3, response->metadataMapsDecodedCount());
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
  ASSERT_TRUE(response->waitForReset());
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
  EXPECT_EQ(upstream_request_->metadataMap().find("key")->second, "value");
  EXPECT_EQ(upstream_request_->metadataMap().size(), 1);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("key")->second, 3);

  ASSERT_TRUE(response->waitForEndStream());
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
  EXPECT_EQ(upstream_request_->metadataMap().find("key")->second, value);
  EXPECT_EQ(upstream_request_->metadataMap().size(), 1);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("key")->second, 3);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(Http2MetadataIntegrationTest, RequestMetadataReachSizeLimit) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

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
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  ASSERT_FALSE(response->complete());
}

TEST_P(Http2MetadataIntegrationTest, RequestMetadataThenTrailers) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Http::MetadataMap metadata_map = {{"key", "value"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

static std::string request_metadata_filter = R"EOF(
name: request-metadata-filter
)EOF";

TEST_P(Http2MetadataIntegrationTest, ConsumeAndInsertRequestMetadata) {
  prependFilters({request_metadata_filter});
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Verifies a headers metadata added.
  std::set<std::string> expected_metadata_keys = {"headers"};
  expected_metadata_keys.insert("metadata");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);

  // Sends a headers only request with metadata. An empty data frame carries end_stream.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  response = std::move(encoder_decoder.second);
  Http::MetadataMap metadata_map = {{"consume", "consume"}};
  codec_client_->sendMetadata(*request_encoder_, metadata_map);
  codec_client_->sendData(*request_encoder_, 0, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("data");
  expected_metadata_keys.insert("metadata");
  expected_metadata_keys.insert("replace");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("metadata")->second, 3);
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("metadata")->second, 4);

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  expected_metadata_keys.erase("trailers");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);
  EXPECT_GE(upstream_request_->duplicatedMetadataKeyCount().find("data")->second, 2);
  EXPECT_GE(upstream_request_->duplicatedMetadataKeyCount().find("metadata")->second, 3);

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  expected_metadata_keys.insert("metadata1");
  expected_metadata_keys.insert("metadata2");
  expected_metadata_keys.insert("trailers");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("metadata")->second, 6);
}

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

void Http2MetadataIntegrationTest::verifyHeadersOnlyTest() {
  // Verifies a headers metadata added.
  std::set<std::string> expected_metadata_keys = {"headers"};
  expected_metadata_keys.insert("metadata");
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);

  // Verifies zero length data received, and end_stream is true.
  EXPECT_EQ(true, upstream_request_->receivedData());
  EXPECT_EQ(0, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());
}

TEST_P(Http2MetadataIntegrationTest, HeadersOnlyRequestWithRequestMetadata) {
  prependFilters({request_metadata_filter});
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  std::set<std::string> expected_metadata_keys = {"headers",   "data",    "metadata", "metadata1",
                                                  "metadata2", "replace", "trailers"};
  verifyExpectedMetadata(upstream_request_->metadataMap(), expected_metadata_keys);
  EXPECT_EQ(upstream_request_->duplicatedMetadataKeyCount().find("metadata")->second, 6);
}

static std::string metadata_stop_all_filter = R"EOF(
name: metadata-stop-all-filter
)EOF";

TEST_P(Http2MetadataIntegrationTest, RequestMetadataWithStopAllFilterBeforeMetadataFilter) {
  prependFilters({request_metadata_filter, metadata_stop_all_filter});
  testRequestMetadataWithStopAllFilter();
}

TEST_P(Http2MetadataIntegrationTest, RequestMetadataWithStopAllFilterAfterMetadataFilter) {
  prependFilters({metadata_stop_all_filter, request_metadata_filter});
  testRequestMetadataWithStopAllFilter();
}

TEST_P(Http2MetadataIntegrationTest, TestAddEncodedMetadata) {
  config_helper_.prependFilter(R"EOF(
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

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(response->metadataMap().find("headers")->second, "headers");
  EXPECT_EQ(response->metadataMap().find("data")->second, "data");
  EXPECT_EQ(response->metadataMap().find("trailers")->second, "trailers");
  EXPECT_EQ(response->metadataMap().size(), 3);
  EXPECT_EQ(count * size + added_decoded_data_size * 2, response->body().size());
}

TEST_P(MultiplexedIntegrationTest, GrpcRouterNotFound) {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/service/notfound", "", downstream_protocol_, version_, "host",
      Http::Headers::get().ContentTypeValues.Grpc);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, response->headers().getContentTypeValue());
  EXPECT_EQ("12", response->headers().getGrpcStatusValue());
}

TEST_P(MultiplexedIntegrationTest, GrpcRetry) { testGrpcRetry(); }

// Verify the case where there is an HTTP/2 codec/protocol error with an active stream.
TEST_P(MultiplexedIntegrationTest, CodecErrorAfterStreamStart) {
  EXCLUDE_DOWNSTREAM_HTTP3; // The HTTP/3 client has no "bad frame" equivalent.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  // Send bogus raw data on the connection.
  Buffer::OwnedImpl bogus_data("some really bogus data");
  codec_client_->rawConnection().write(bogus_data, false);

  // Verifies error is received.
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(MultiplexedIntegrationTest, Http2BadMagic) {
  config_helper_.disableDelayClose();
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    // The "magic" payload is an HTTP/2 specific thing.
    return;
  }
  initialize();
  std::string response;
  auto connection = createConnectionDriver(
      lookupPort("http"), "hello",
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });
  ASSERT_TRUE(connection->run());
  EXPECT_EQ("", response);
}

TEST_P(MultiplexedIntegrationTest, BadFrame) {
  EXCLUDE_DOWNSTREAM_HTTP3; // The HTTP/3 client has no "bad frame" equivalent.

  initialize();
  std::string response;
  auto connection = createConnectionDriver(
      lookupPort("http"), "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror",
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });
  ASSERT_TRUE(connection->run());
  if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    EXPECT_THAT(response, HasSubstr("ParseError"));
  } else {
    EXPECT_THAT(response, HasSubstr("SETTINGS expected"));
  }
}

// Send client headers, a GoAway and then a body and ensure the full request and
// response are received.
TEST_P(MultiplexedIntegrationTest, GoAway) {
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->goAway();
  codec_client_->sendData(*request_encoder_, 0, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// TODO(rch): Add a unit test which covers internal redirect handling.
TEST_P(MultiplexedIntegrationTestWithSimulatedTime, GoAwayAfterTooManyResets) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Need to wait for the server to reset the stream
                            // before opening new one.
  config_helper_.addRuntimeOverride("envoy.restart_features.send_goaway_for_premature_rst_streams",
                                    "true");
  const int total_streams = 100;
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count",
                                    absl::StrCat(total_streams));
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (int i = 0; i < total_streams; ++i) {
    // Send and wait
    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    codec_client_->sendData(*request_encoder_, 0, true);
    auto response = std::move(encoder_decoder.second);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
  }
  for (int i = 0; i < total_streams; ++i) {
    // Send and reset
    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendReset(*request_encoder_);
    ASSERT_TRUE(response->waitForReset());
  }

  // Envoy should disconnect client due to premature reset check
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterEq("http.config_test.downstream_rq_rx_reset", total_streams);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_too_many_premature_resets", 1);
}

TEST_P(MultiplexedIntegrationTestWithSimulatedTime, GoAwayQuicklyAfterTooManyResets) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Need to wait for the server to reset the stream
                            // before opening new one.
  config_helper_.addRuntimeOverride("envoy.restart_features.send_goaway_for_premature_rst_streams",
                                    "true");
  const int total_streams = 100;
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count",
                                    absl::StrCat(total_streams));
  const int num_reset_streams = total_streams / 2;
  initialize();

  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int i = 0; i < num_reset_streams; i++) {
    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendReset(*request_encoder_);
    ASSERT_TRUE(response->waitForReset());
  }

  // Envoy should disconnect client due to premature reset check
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterEq("http.config_test.downstream_rq_rx_reset", num_reset_streams);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_too_many_premature_resets", 1);
}

TEST_P(MultiplexedIntegrationTestWithSimulatedTime, DontGoAwayAfterTooManyResetsForLongStreams) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Need to wait for the server to reset the stream
                            // before opening new one.
  config_helper_.addRuntimeOverride("envoy.restart_features.send_goaway_for_premature_rst_streams",
                                    "true");
  const int total_streams = 100;
  const int stream_lifetime_seconds = 2;
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count",
                                    absl::StrCat(total_streams));

  config_helper_.addRuntimeOverride("overload.premature_reset_min_stream_lifetime_seconds",
                                    absl::StrCat(stream_lifetime_seconds));

  initialize();

  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string request_counter = "http.config_test.downstream_rq_total";
  std::string reset_counter = "http.config_test.downstream_rq_rx_reset";
  for (int i = 0; i < total_streams * 2; ++i) {
    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    test_server_->waitForCounterEq(request_counter, i + 1);
    timeSystem().advanceTimeWait(std::chrono::seconds(2 * stream_lifetime_seconds));
    codec_client_->sendReset(*request_encoder_);
    ASSERT_TRUE(response->waitForReset());
    test_server_->waitForCounterEq(reset_counter, i + 1);
  }
}

TEST_P(MultiplexedIntegrationTest, Trailers) { testTrailers(1024, 2048, false, false); }

TEST_P(MultiplexedIntegrationTest, TrailersGiantBody) {
  testTrailers(1024 * 1024, 1024 * 1024, false, false);
}

// Ensure if new timeouts are set, legacy timeouts do not apply.
TEST_P(MultiplexedIntegrationTest, DEPRECATED_FEATURE_TEST(GrpcRequestTimeoutMixedLegacy)) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        route->mutable_route()->mutable_max_grpc_timeout()->set_nanos(1000 * 1000);
        route->mutable_route()
            ->mutable_max_stream_duration()
            ->mutable_grpc_timeout_header_max()
            ->set_seconds(60 * 60);
      });

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"te", "trailers"},
                                     {"grpc-timeout", "2S"}, // 2 Second
                                     {"content-type", "application/grpc"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("via_upstream"));
}

TEST_P(MultiplexedIntegrationTest, GrpcRequestTimeout) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        route->mutable_route()
            ->mutable_max_stream_duration()
            ->mutable_grpc_timeout_header_max()
            ->set_seconds(60 * 60);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"te", "trailers"},
                                     {"grpc-timeout", "1S"}, // 1 Second
                                     {"content-type", "application/grpc"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_NE(response->headers().GrpcStatus(), nullptr);
  EXPECT_EQ("4", response->headers().getGrpcStatusValue()); // Deadline exceeded.
  EXPECT_LT(0,
            test_server_->counter("http.config_test.downstream_rq_max_duration_reached")->value());
}

// Interleave two requests and responses and make sure that idle timeout is handled correctly.
TEST_P(MultiplexedIntegrationTest, IdleTimeoutWithSimultaneousRequests) {
  FakeHttpConnectionPtr fake_upstream_connection1;
  FakeHttpConnectionPtr fake_upstream_connection2;
  Http::RequestEncoder* encoder1;
  Http::RequestEncoder* encoder2;
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  int32_t request1_bytes = 1024;
  int32_t request2_bytes = 512;

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());

    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
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
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Validate that idle time is not kicked in.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_idle_timeout")->value());
  EXPECT_NE(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(request1_bytes, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(request1_bytes, response1->body().size());

  // Do not send any requests and validate idle timeout kicks in after both the requests are done.
  ASSERT_TRUE(fake_upstream_connection1->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 2);
}

// Test request mirroring / shadowing with an HTTP/2 downstream and a request with a body.
TEST_P(MultiplexedIntegrationTest, RequestMirrorWithBody) {
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
  IntegrationStreamDecoderPtr response =
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
  EXPECT_EQ("host-shadow", upstream_request2->headers().getHostValue());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Cleanup.
  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
}

// Interleave two requests and responses and make sure the HTTP2 stack handles this correctly.
void MultiplexedIntegrationTest::simultaneousRequest(int32_t request1_bytes,
                                                     int32_t request2_bytes) {
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
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  EXPECT_EQ(request2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(request2_bytes, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(request2_bytes, response1->body().size());

  // Cleanup both downstream and upstream
  ASSERT_TRUE(fake_upstream_connection1->close());
  ASSERT_TRUE(fake_upstream_connection1->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  codec_client_->close();
}

TEST_P(MultiplexedIntegrationTest, SimultaneousRequest) { simultaneousRequest(1024, 512); }

TEST_P(MultiplexedIntegrationTest, SimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 32, 1024 * 16);
}

// Test downstream connection delayed close processing.
TEST_P(MultiplexedIntegrationTest, DelayedCloseAfterBadFrame) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Needs HTTP/3 "bad frame" equivalent.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_nanos(1000 * 1000); });
  initialize();
  std::string response;

  auto connection = createConnectionDriver(
      lookupPort("http"), "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror",
      [&](Network::ClientConnection& connection, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        connection.dispatcher().exit();
      });

  ASSERT_TRUE(connection->run());
  if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    EXPECT_THAT(response, HasSubstr("ParseError"));
  } else {
    EXPECT_THAT(response, HasSubstr("SETTINGS expected"));
  }
  // Due to the multiple dispatchers involved (one for the RawConnectionDriver and another for the
  // Envoy server), it's possible the delayed close timer could fire and close the server socket
  // prior to the data callback above firing. Therefore, we may either still be connected, or have
  // received a remote close.
  if (connection->lastConnectionEvent() == Network::ConnectionEvent::Connected) {
    ASSERT_TRUE(connection->run());
  }
  EXPECT_EQ(connection->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

// Test disablement of delayed close processing on downstream connections.
TEST_P(MultiplexedIntegrationTest, DelayedCloseDisabled) {
  EXCLUDE_DOWNSTREAM_HTTP3; // Needs HTTP/3 "bad frame" equivalent.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_nanos(0); });
  initialize();
  std::string response;
  auto connection = createConnectionDriver(
      lookupPort("http"), "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror",
      [&](Network::ClientConnection& connection, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        connection.dispatcher().exit();
      });

  ASSERT_TRUE(connection->run());
  if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    EXPECT_THAT(response, HasSubstr("ParseError"));
  } else {
    EXPECT_THAT(response, HasSubstr("SETTINGS expected"));
  }
  // Due to the multiple dispatchers involved (one for the RawConnectionDriver and another for the
  // Envoy server), it's possible for the 'connection' to receive the data and exit the dispatcher
  // prior to the FIN being received from the server.
  if (connection->lastConnectionEvent() == Network::ConnectionEvent::Connected) {
    ASSERT_TRUE(connection->run());
  }
  EXPECT_EQ(connection->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            0);
}

TEST_P(MultiplexedIntegrationTest, PauseAndResume) {
  config_helper_.prependFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(MultiplexedIntegrationTest, PauseAndResumeHeadersOnly) {
  config_helper_.prependFilter(R"EOF(
  name: stop-iteration-and-continue-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.StopAndContinueConfig
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

// Verify the case when we have large pending data with empty trailers. It should not introduce
// stack-overflow (on ASan build). This is a regression test for
// https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=24714.
TEST_P(MultiplexedIntegrationTest, EmptyTrailers) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 100000, false);
  Http::TestRequestTrailerMapImpl request_trailers;
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(MultiplexedIntegrationTest, TestEncode1xxHeaders) {
  static std::string encode1xx_local_reply_config = R"EOF(
  name: encode1xx-local-reply-filter
  )EOF";
  config_helper_.prependFilter(encode1xx_local_reply_config);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with 100-continue, which the filter turns into a local
  // reply.
  auto response =
      codec_client_->makeRequestWithBody(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"},
                                                                        {"expect", "100-continue"}},
                                         10);

  waitForNextUpstreamRequest();
  // This will trigger local reply response.
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

class MultiplexedRingHashIntegrationTest : public HttpProtocolIntegrationTest {
public:
  MultiplexedRingHashIntegrationTest();

  ~MultiplexedRingHashIntegrationTest() override;

  void createUpstreams() override;

  void sendMultipleRequests(int request_bytes, Http::TestRequestHeaderMapImpl headers,
                            std::function<void(IntegrationStreamDecoder&)> cb);

  std::vector<FakeHttpConnectionPtr> fake_upstream_connections_;
  int num_upstreams_ = 5;
};

MultiplexedRingHashIntegrationTest::MultiplexedRingHashIntegrationTest() {
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

MultiplexedRingHashIntegrationTest::~MultiplexedRingHashIntegrationTest() {
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

void MultiplexedRingHashIntegrationTest::createUpstreams() {
  for (int i = 0; i < num_upstreams_; i++) {
    addFakeUpstream(Http::CodecType::HTTP1);
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiplexedRingHashIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2, Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2MetadataIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

void MultiplexedRingHashIntegrationTest::sendMultipleRequests(
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
    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_requests.back()));
    upstream_requests.back()->setAddServedByHeader(true);
    fake_upstream_connections_.push_back(std::move(fake_upstream_connection));
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(upstream_requests[i]->waitForEndStream(*dispatcher_));
    upstream_requests[i]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_requests[i]->encodeData(rand.random() % (1024 * 2), true);
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(responses[i]->waitForEndStream());
    EXPECT_TRUE(upstream_requests[i]->complete());
    EXPECT_EQ(request_bytes, upstream_requests[i]->bodyLength());

    EXPECT_TRUE(responses[i]->complete());
    cb(*responses[i]);
  }
}

TEST_P(MultiplexedRingHashIntegrationTest, CookieRoutingNoCookieNoTtl) {
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
        EXPECT_EQ("200", response.headers().getStatusValue());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie).empty());
        served_by.insert(std::string(response.headers()
                                         .get(Http::LowerCaseString("x-served-by"))[0]
                                         ->value()
                                         .getStringView()));
      });
  EXPECT_EQ(served_by.size(), num_upstreams_);
}

TEST_P(MultiplexedRingHashIntegrationTest, CookieRoutingNoCookieWithNonzeroTtlSet) {
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
        EXPECT_EQ("200", response.headers().getStatusValue());
        std::string value(
            response.headers().get(Http::Headers::get().SetCookie)[0]->value().getStringView());
        set_cookies.insert(value);
        EXPECT_THAT(value, MatchesRegex("foo=.*; Max-Age=15; HttpOnly"));
      });
  EXPECT_EQ(set_cookies.size(), 1);
}

TEST_P(MultiplexedRingHashIntegrationTest, CookieRoutingNoCookieWithZeroTtlSet) {
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
        EXPECT_EQ("200", response.headers().getStatusValue());
        std::string value(
            response.headers().get(Http::Headers::get().SetCookie)[0]->value().getStringView());
        set_cookies.insert(value);
        EXPECT_THAT(value, MatchesRegex("^foo=.*$"));
      });
  EXPECT_EQ(set_cookies.size(), 1);
}

TEST_P(MultiplexedRingHashIntegrationTest, CookieRoutingWithCookieNoTtl) {
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
        EXPECT_EQ("200", response.headers().getStatusValue());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie).empty());
        served_by.insert(std::string(response.headers()
                                         .get(Http::LowerCaseString("x-served-by"))[0]
                                         ->value()
                                         .getStringView()));
      });
  EXPECT_EQ(served_by.size(), 1);
}

TEST_P(MultiplexedRingHashIntegrationTest, CookieRoutingWithCookieWithTtlSet) {
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
        EXPECT_EQ("200", response.headers().getStatusValue());
        EXPECT_TRUE(response.headers().get(Http::Headers::get().SetCookie).empty());
        served_by.insert(std::string(response.headers()
                                         .get(Http::LowerCaseString("x-served-by"))[0]
                                         ->value()
                                         .getStringView()));
      });
  EXPECT_EQ(served_by.size(), 1);
}

struct FrameIntegrationTestParam {
  Network::Address::IpVersion ip_version;
  Http2Impl http2_implementation;
};

std::string
frameIntegrationTestParamToString(const testing::TestParamInfo<FrameIntegrationTestParam>& params) {
  return absl::StrCat(TestUtility::ipVersionToString(params.param.ip_version), "_",
                      http2ImplementationToString(params.param.http2_implementation));
}

class Http2FrameIntegrationTest : public testing::TestWithParam<FrameIntegrationTestParam>,
                                  public Http2RawFrameIntegrationTest {
public:
  Http2FrameIntegrationTest() : Http2RawFrameIntegrationTest(GetParam().ip_version) {
    setupHttp2ImplOverrides(GetParam().http2_implementation);
  }

  static std::vector<FrameIntegrationTestParam> testParams() {
    std::vector<FrameIntegrationTestParam> v;
    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      v.push_back({ip_version, Http2Impl::Nghttp2});
      v.push_back({ip_version, Http2Impl::Oghttp2});
    }
    return v;
  }
};

TEST_P(Http2FrameIntegrationTest, MaxConcurrentStreamsIsRespected) {
  const int kTotalRequests = 101;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_max_concurrent_streams()->set_value(100);
      });
  beginSession();

  std::string buffer;
  for (int i = 0; i < kTotalRequests; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/");
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  tcp_client_->waitForDisconnect();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy_local", 1);
}

// Regression test.
TEST_P(Http2FrameIntegrationTest, SetDetailsTwice) {
  autonomous_upstream_ = true;
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  // Send two concatenated frames, the first with too many headers, and the second an invalid frame
  // (push_promise)
  std::string bad_frame =
      "00006d0104000000014083a8749783ee3a3fbebebebebebebebebebebebebebebebebebebebebebebebebebebebe"
      "bebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebe"
      "bebebebebebebebebebebebebebebebebebebebebebebebebebe0001010500000000018800a065";
  Http2Frame request = Http2Frame::makeGenericFrameFromHexDump(bad_frame);
  sendFrame(request);
  tcp_client_->close();

  // Expect that the details for the first frame are kept.
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("too_many_headers"));
}

TEST_P(Http2FrameIntegrationTest, AdjustUpstreamSettingsMaxStreams) {
  // Configure max concurrent streams to 2.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->mutable_max_concurrent_streams()
        ->set_value(2);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  beginSession();
  FakeRawConnectionPtr fake_upstream_connection1;

  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(1, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
  const Http2Frame settings_frame = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1}});
  std::string settings_data(settings_frame);
  ASSERT_TRUE(fake_upstream_connection1->write(settings_data));
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total",
                                 settings_data.size());
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);

  // Start another request, it should create another upstream connection because of the max
  // concurrent streams of upstream connection created above.
  FakeRawConnectionPtr fake_upstream_connection2;
  sendFrame(Http2Frame::makePostRequest(3, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->write(std::string(settings_frame)));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 2);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 2);

  // Adjust the max concurrent streams of one connection created above to 2.
  auto bytes_read = test_server_->counter("cluster.cluster_0.upstream_cx_rx_bytes_total");
  const Http2Frame settings_frame2 = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 3}});
  std::string settings_data2(settings_frame2);
  ASSERT_TRUE(fake_upstream_connection1->write(settings_data2));
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total",
                                 bytes_read + settings_data2.size());
  // Now create another request.
  sendFrame(Http2Frame::makePostRequest(5, "host", "/path/to/long/url"));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 3);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 2);

  // The configured max concurrent streams is 2, even the SETTINGS frame above wants to
  // set the max concurrent streams to 3, it still reaches the upper bound. So the new request
  // below should result in the third connection.
  sendFrame(Http2Frame::makePostRequest(7, "host", "/path/to/long/url"));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 4);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 3);

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, UpstreamSettingsMaxStreamsAfterGoAway) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = 1;
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);

  // Send RST_STREAM, GOAWAY and SETTINGS(0 max streams)
  const Http2Frame rst_stream =
      Http2Frame::makeResetStreamFrame(client_stream_idx, Http2Frame::ErrorCode::FlowControlError);
  const Http2Frame go_away_frame =
      Http2Frame::makeEmptyGoAwayFrame(12345, Http2Frame::ErrorCode::NoError);
  const Http2Frame settings_max_connections_frame = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0}});
  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(rst_stream), std::string(go_away_frame),
                   std::string(settings_max_connections_frame))));

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_close_notify", 1);

  // Cleanup.
  tcp_client_->close();
}

// Verify that receiving a WINDOW_UPDATE frame after a GOAWAY does not trigger an assertion failure
// complaining of continued dispatch after connection close.
TEST_P(Http2FrameIntegrationTest, UpstreamWindowUpdateAfterGoAway) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = Http2Frame::makeClientStreamId(0);
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);

  // Start a second request and wait for it to reach the upstream. This is to exercise the case
  // where numActiveRequests > 0 at the time that GOAWAY is received from upstream, which is needed
  // to replicate the original assertion failure.
  sendFrame(
      Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(1), "host", "/path/to/long/url"));
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 2);

  // Send RST_STREAM, GOAWAY followed by WINDOW_UPDATE
  const Http2Frame rst_stream =
      Http2Frame::makeResetStreamFrame(client_stream_idx, Http2Frame::ErrorCode::FlowControlError);
  // Since last_stream_index <= the stream IDs of all active streams, this
  // results in all active streams being closed, so the connection gets closed
  // as well.
  const Http2Frame go_away_frame = Http2Frame::makeEmptyGoAwayFrame(
      /*last_stream_index=*/client_stream_idx, Http2Frame::ErrorCode::NoError);
  const Http2Frame window_update_frame = Http2Frame::makeWindowUpdateFrame(0, 10);
  ASSERT_TRUE(fake_upstream_connection->write(absl::StrCat(
      std::string(rst_stream), std::string(go_away_frame), std::string(window_update_frame))));

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_close_notify", 1);

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, AccessLogOfWireBytesIfResponseSizeGreaterThanWindowSize) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        // We need to increase the idle timeout to avoid connection close.
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->set_seconds(10);
      });
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_SENT%");
  beginSession();

  // Sending a settings frame to change window to be less than the response
  // size.
  // Wait for Envoy to ack the renegotiated settings.
  const Http2Frame settings_frame2 = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 70000}});
  sendFrame(settings_frame2);

  auto renegotiated_setting = readFrame();
  EXPECT_EQ(Http2Frame::Type::Settings, renegotiated_setting.type());

  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makeRequest(1, "host", "/response/larger/than/window"));
  waitForNextUpstreamRequest();
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(60000, false);
  upstream_request_->encodeData(50000, true);

  // Wire bytes received *ONLY* relates to wire bytes for this stream e.g. connection
  // level frames are irrelevant.
  StreamByteAccumulator accumulator;

  Http2Frame response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  accumulator.countFrame(response);

  response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Data, response.type());
  accumulator.countFrame(response);

  response = readFrame();
  accumulator.countFrame(response);
  EXPECT_EQ(Http2Frame::Type::Data, response.type());

  // Check access log if the agnostic stream lifetime is not extended.
  // It should have access logged since it has received the entire response.
  int hcm_logged_wire_bytes_sent, hcm_logged_wire_header_bytes_sent;

  // Grant the sender (Envoy) additional window so it can finish sending the
  // stream.
  const Http2Frame stream_update_frame = Http2Frame::makeWindowUpdateFrame(1, 60000);
  const Http2Frame conn_update_frame = Http2Frame::makeWindowUpdateFrame(0, 60000);
  sendFrame(conn_update_frame);
  sendFrame(stream_update_frame);

  while (!response.endStream() && accumulator.stream_wire_bytes_recieved_ < 60000 + 50000) {
    response = readFrame();
    accumulator.countFrame(response);
    EXPECT_EQ(Http2Frame::Type::Data, response.type());
  }

  EXPECT_EQ(accumulator.bodyWireBytesReceivedDiscountingHeaders(),
            accumulator.bodyWireBytesReceivedGivenPayloadAndFrames());

  // Access logs are only available now due to the expanded agnostic stream
  // lifetime.
  auto access_log_values = stoiAccessLogString(waitForAccessLog(access_log_name_));
  hcm_logged_wire_bytes_sent = access_log_values[0];
  hcm_logged_wire_header_bytes_sent = access_log_values[1];
  EXPECT_EQ(accumulator.stream_wire_header_bytes_recieved_, hcm_logged_wire_header_bytes_sent);
  EXPECT_EQ(accumulator.stream_wire_bytes_recieved_, hcm_logged_wire_bytes_sent)
      << "Received " << accumulator.stream_wire_bytes_recieved_
      << " stream wire bytes from Envoy but access log reported " << hcm_logged_wire_bytes_sent;

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostDifferentFromAuthority) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "two.example.com"}});
  sendFrame(request);

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "one.example.com,two.example.com");
  upstream_request_->encodeHeaders(default_response_headers_, true);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostSameAsAuthority) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "one.example.com"}});
  sendFrame(request);

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "one.example.com,one.example.com");
  upstream_request_->encodeHeaders(default_response_headers_, true);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleHeaderOnlyRequests) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleRequests) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithMetadata) {
  // Allow metadata usage.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  config_helper_.prependFilter(R"EOF(
  name: metadata-control-filter
  )EOF");

  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    Http::MetadataMap metadata_map{{"should_continue", absl::StrCat(i)}};
    auto metadata = Http2Frame::makeMetadataFrameFromMetadataMap(
        Http2Frame::makeClientStreamId(i), metadata_map, Http2Frame::MetadataFlags::EndMetadata);
    absl::StrAppend(&buffer, std::string(metadata));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

// Validate the request completion during processing of deferred list works.
TEST_P(Http2FrameIntegrationTest, MultipleRequestsDecodeHeadersEndsRequest) {
  const int kRequestsSentPerIOCycle = 20;
  // The local-reply-during-decode will call sendLocalReply, completing them
  // when processing headers. This will cause the ConnectionManagerImpl::ActiveRequest
  // object to be removed from the streams_ list during the onDeferredRequestProcessing call.
  config_helper_.addFilter("{ name: local-reply-during-decode }");
  // Process more than 1 deferred request at a time to validate the removal of elements from
  // the list does not break reverse iteration.
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "3");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  // The local-reply-during-decode filter sends 500 status to the client
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::InternalServerError, frame.responseStatus());
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithTrailers) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a");
    absl::StrAppend(&buffer, std::string(data));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto trailers = Http2Frame::makeEmptyHeadersFrame(
        Http2Frame::makeClientStreamId(i),
        static_cast<Http2Frame::HeadersFlags>(Http::Http2::orFlags(
            Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));
    trailers.appendHeaderWithoutIndexing({"k", "v"});
    trailers.adjustPayloadSize();
    absl::StrAppend(&buffer, std::string(trailers));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

// Validate the request completion during processing of headers in the deferred requests,
// is ok, when deferred data and trailers are also present.
TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithTrailersDecodeHeadersEndsRequest) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addFilter("{ name: local-reply-during-decode }");
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "6");
  beginSession();

  std::string buffer;
  // Make every 4th request to be reset by the local-reply-during-decode filter, this will give a
  // good distribution of removed requests from the deferred sequence.
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                               {{"response_data_blocks", "0"},
                                                {"no_trailers", "1"},
                                                {"skip-local-reply", i % 4 ? "true" : "false"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a");
    absl::StrAppend(&buffer, std::string(data));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto trailers = Http2Frame::makeEmptyHeadersFrame(
        Http2Frame::makeClientStreamId(i),
        static_cast<Http2Frame::HeadersFlags>(Http::Http2::orFlags(
            Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));
    trailers.appendHeaderWithoutIndexing({"k", "v"});
    trailers.adjustPayloadSize();
    absl::StrAppend(&buffer, std::string(trailers));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    uint32_t stream_id = frame.streamId();
    // Client stream indexes are multiples of 2 starting at 1
    if ((stream_id / 2) % 4) {
      EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus())
          << " for stream=" << stream_id;
    } else {
      EXPECT_EQ(Http2Frame::ResponseStatus::InternalServerError, frame.responseStatus())
          << " for stream=" << stream_id;
    }
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleHeaderOnlyRequestsFollowedByReset) {
  // This number of requests stays below premature reset detection.
  const int kRequestsSentPerIOCycle = 20;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto reset = Http2Frame::makeResetStreamFrame(Http2Frame::makeClientStreamId(i),
                                                  Http2Frame::ErrorCode::Cancel);
    absl::StrAppend(&buffer, std::string(reset));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_rx_reset",
                                 kRequestsSentPerIOCycle);
  // Client should remain connected
  ASSERT_TRUE(tcp_client_->connected());
  tcp_client_->close();
}

// This test depends on an another patch with premature resets
TEST_P(Http2FrameIntegrationTest, ResettingDeferredRequestsTriggersPrematureResetCheck) {
  const int kRequestsSentPerIOCycle = 20;
  // Set premature stream count to twice the number of streams we are about to send.
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count", "40");
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto reset = Http2Frame::makeResetStreamFrame(Http2Frame::makeClientStreamId(i),
                                                  Http2Frame::ErrorCode::Cancel);
    absl::StrAppend(&buffer, std::string(reset));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  // Envoy should close the client connection due to too many premature resets
  tcp_client_->waitForDisconnect();
  test_server_->waitForCounterEq("http.config_test.downstream_rq_too_many_premature_resets", 1);
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, CloseConnectionWithDeferredStreams) {
  // Use large number of requests to ensure close is detected while there are
  // still some deferred streams.
  const int kRequestsSentPerIOCycle = 20000;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  // Ensure premature reset detection does not get in the way
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count", "1001");
  // Disable the request timeout, iouring may failed the test due to request timeout.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_timeout()
            ->set_seconds(0);
      });
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/");
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  ASSERT_TRUE(tcp_client_->connected());
  // Drop the downstream connection
  tcp_client_->close();
  // Test that Envoy can clean-up deferred streams
  // Make the timeout longer to accommodate non optimized builds
  test_server_->waitForCounterEq("http.config_test.downstream_rq_rx_reset", kRequestsSentPerIOCycle,
                                 TestUtility::DefaultTimeout * 3);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2FrameIntegrationTest,
                         testing::ValuesIn(Http2FrameIntegrationTest::testParams()),
                         frameIntegrationTestParamToString);

// Tests sending an empty metadata map from downstream.
TEST_P(Http2FrameIntegrationTest, DownstreamSendingEmptyMetadata) {
  // Allow metadata usage.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  // This test uses an Http2Frame and not the encoder's encodeMetadata method,
  // because encodeMetadata fails when an empty metadata map is sent.
  beginSession();

  const uint32_t client_stream_idx = 1;
  // Send request.
  const Http2Frame request =
      Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url");
  sendFrame(request);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send metadata frame with empty metadata map.
  const Http::MetadataMap empty_metadata_map;
  const Http2Frame empty_metadata_map_frame = Http2Frame::makeMetadataFrameFromMetadataMap(
      client_stream_idx, empty_metadata_map, Http2Frame::MetadataFlags::EndMetadata);
  sendFrame(empty_metadata_map_frame);

  // Send an empty data frame to close the stream.
  const Http2Frame empty_data_frame =
      Http2Frame::makeEmptyDataFrame(client_stream_idx, Http2Frame::DataFlags::EndStream);
  sendFrame(empty_data_frame);

  // Upstream sends a reply.
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Make sure that a response from upstream is received by the client, and
  // close the connection.
  const auto response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, response.responseStatus());
  EXPECT_EQ(1, test_server_->counter("http2.metadata_empty_frames")->value());

  // Cleanup. Closing upstream connection first to avoid a race between the
  // client FIN and the connection closure (see comment in
  // HttpIntegrationTest::cleanupUpstreamAndDownstream).
  cleanupUpstreamAndDownstream();
  tcp_client_->close();
}

// Tests that an empty metadata map from upstream is ignored.
TEST_P(Http2MetadataIntegrationTest, UpstreamSendingEmptyMetadata) {
  initialize();

  // Send a request and make sure an upstream connection is established.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  auto* upstream = fake_upstreams_.front().get();

  // Send response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  // Send an empty metadata map back from upstream.
  const Http::MetadataMap empty_metadata_map;
  const Http2Frame empty_metadata_frame = Http2Frame::makeMetadataFrameFromMetadataMap(
      1, empty_metadata_map, Http2Frame::MetadataFlags::EndMetadata);
  ASSERT_TRUE(upstream->rawWriteConnection(
      0, std::string(empty_metadata_frame.begin(), empty_metadata_frame.end())));
  // Send an empty data frame after the metadata frame to end the stream.
  upstream_request_->encodeData(0, true);

  // Verifies that no metadata was received by the client.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(0, response->metadataMapsDecodedCount());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http2.metadata_empty_frames")->value());
}

// Tests upstream sending a metadata frame after ending a stream.
TEST_P(Http2MetadataIntegrationTest, UpstreamMetadataAfterEndStream) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends the first request.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);

  // Wait for upstream to receive the request
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Upstream sends headers and ends stream.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Upstream sends metadata.
  const Http::MetadataMap response_metadata_map = {{"resp_key1", "resp_value1"}};
  Http::MetadataMapPtr metadata_map_ptr =
      std::make_unique<Http::MetadataMap>(response_metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  upstream_request_->encodeMetadata(metadata_map_vector);

  // Cleanup.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(MultiplexedIntegrationTest, InvalidTrailers) {
  disable_client_header_validation_ = true;
  autonomous_allow_incomplete_streams_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  autonomous_upstream_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  std::string value = std::string(1, 2);
  EXPECT_FALSE(Http::HeaderUtility::headerValueIsValid(value));
  codec_client_->sendTrailers(*request_encoder_,
                              Http::TestRequestTrailerMapImpl{{"trailer", value}});
  ASSERT_TRUE(response->waitForReset());
  // http2.invalid.header.field or http3.invalid_header_field
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid"));
}

TEST_P(MultiplexedIntegrationTest, InconsistentContentLength) {
#ifdef ENVOY_ENABLE_UHV
  if (GetParam().http2_implementation == Http2Impl::Oghttp2 &&
      downstreamProtocol() == Http::CodecType::HTTP2) {
    return;
  }
#endif

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"content-length", "1025"}});

  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 1024, false);
  codec_client_->sendTrailers(*request_encoder_,
                              Http::TestRequestTrailerMapImpl{{"trailer", "value"}});

  // Inconsistency in content-length header and the actually body length should be treated as a
  // stream error.
  ASSERT_TRUE(response->waitForReset());
  // http3.inconsistent_content_length.
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->resetReason());
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("inconsistent_content_length"));
  } else if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->resetReason());
    EXPECT_THAT(waitForAccessLog(access_log_name_), "http2.violation.of.messaging.rule");
  } else {
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
    // http2.violation.of.messaging.rule
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("violation"));
  }
}

// HTTP/2 and HTTP/3 don't support 101 SwitchProtocol response code, the client should
// reset the request.
TEST_P(MultiplexedIntegrationTest, Reset101SwitchProtocolResponse) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(#29071): reject responses with 101 in H/2 and H/3 UHV
  if (GetParam().http2_implementation == Http2Impl::Oghttp2 &&
      downstreamProtocol() == Http::CodecType::HTTP2) {
    return;
  }
#endif

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"expect", "100-continue"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "101"}}, false);
  ASSERT_TRUE(response->waitForReset());
  codec_client_->close();
  EXPECT_FALSE(response->complete());
}

TEST_P(MultiplexedIntegrationTest, PerTryTimeoutWhileDownstreamStopsReading) {
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.upstream_wait_for_response_headers_before_disabling_read", "true");

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->listeners_size() >= 1, "");
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    // Config a smaller connection send buffer size.
    listener->mutable_per_connection_buffer_limit_bytes()->set_value(512 * 1024);
  });

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0)->mutable_route();
        auto* retry_policy = route->mutable_retry_policy();
        // Config an aggressive per try timeout and no retry.
        retry_policy->mutable_num_retries()->set_value(0);
        retry_policy->mutable_per_try_timeout()->set_seconds(1);
        // Make sure other timeouts won't interfere.
        route->mutable_timeout()->set_seconds(15);
      });
  autonomous_upstream_ = false;
  initialize();

  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));

  codec_client_ = makeHttpConnection(makeClientConnectionWithOptions(lookupPort("http"), options));

  Envoy::IntegrationStreamDecoderPtr response1 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url1"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(5 * 1024 * 1024, true);

  // Downstream stops reading so that the Envoy's connection send buffer builds up by response1.
  codec_client_->connection()->readDisable(true);

  Envoy::IntegrationStreamDecoderPtr response2 = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url2"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  FakeHttpConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));

  Stats::CounterSharedPtr upstream_read_disabled_counter;
  while (!response1->reset() && !response2->reset() && !response1->complete()) {
    // Check upstream flow control condition every 10ms and exit the loop if upstream paused
    // reading.
    if (upstream_read_disabled_counter == nullptr) {
      upstream_read_disabled_counter = Envoy::TestUtility::findCounter(
          test_server_->statStore(),
          "cluster.cluster_0.upstream_flow_control_paused_reading_total");
    }
    if (upstream_read_disabled_counter != nullptr && upstream_read_disabled_counter->value() >= 1) {
      upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
      upstream_request2->encodeData(1024 * 1024, true);
      break;
    }
    dispatcher_->run(Envoy::Event::Dispatcher::RunType::NonBlock);
    absl::SleepFor(absl::Milliseconds(10));
  }
  EXPECT_FALSE(response1->complete() || response1->reset());
  EXPECT_FALSE(response2->reset());
  // Wait for 2s to make sure pre try timeout doesn't reset the 2nd request.
  absl::SleepFor(absl::Seconds(2));
  codec_client_->connection()->readDisable(false);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ(response2->headers().Status()->value().getStringView(), "200");
  if (!response1->complete()) {
    ASSERT_TRUE(response1->waitForEndStream());
  }
}

TEST_P(MultiplexedIntegrationTest, ConnectionPoolPerDownstream) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->set_connection_pool_per_downstream_connection(true);
  });

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  // Preserve the upstream connection while making sure
  // sendRequestAndWaitForResponse will wait for a new connection.
  FakeHttpConnectionPtr upstream_connection_ptr = std::move(fake_upstream_connection_);
  std::unique_ptr<IntegrationCodecClient> client = std::move(codec_client_);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  AssertionResult result = upstream_connection_ptr->close();
  RELEASE_ASSERT(result, result.message());
  result = upstream_connection_ptr->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  upstream_connection_ptr.reset();
  client->close();
}

// Ordering of inheritance is important here, SocketInterfaceSwap must be
// destroyed after HttpProtocolIntegrationTest.
class SocketSwappableMultiplexedIntegrationTest : public SocketInterfaceSwap,
                                                  public HttpProtocolIntegrationTest {
public:
  SocketSwappableMultiplexedIntegrationTest()
      : SocketInterfaceSwap(GetParam().downstream_protocol == Http::CodecType::HTTP3
                                ? Network::Socket::Type::Datagram
                                : Network::Socket::Type::Stream) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SocketSwappableMultiplexedIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(SocketSwappableMultiplexedIntegrationTest, BackedUpDownstreamConnectionClose) {
  config_helper_.setBufferLimits(1000, 1000);
  autonomous_upstream_ = false;
  initialize();

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto response_decoder = codec_client_->makeRequestWithBody(default_request_headers_, 10);

  waitForNextUpstreamRequest();
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1000, false);

  // We should trigger pause at least once, and eventually have at least 1k
  // bytes buffered.
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  test_server_->waitForGaugeGe("http.config_test.downstream_cx_tx_bytes_buffered", 1000);

  // Close downstream, check cleanup.
  codec_client_->close();

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("http.config_test.downstream_rq_active", 0);
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_tx_bytes_buffered", 0);
}

TEST_P(SocketSwappableMultiplexedIntegrationTest, BackedUpUpstreamConnectionClose) {
  config_helper_.setBufferLimits(1000, 1000);
  autonomous_upstream_ = false;
  initialize();

  // Stop writes to the upstream.
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto [request_encoder, response_decoder] = codec_client_->startRequest(default_request_headers_);
  codec_client_->sendData(request_encoder, 1000, false);

  // We should trigger pause at least once, and eventually have at least 1k
  // bytes buffered.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_backed_up_total", 1);
  test_server_->waitForCounterGe("http.config_test.downstream_flow_control_paused_reading_total",
                                 1);
  test_server_->waitForGaugeGe("cluster.cluster_0.upstream_cx_tx_bytes_buffered", 1000);

  // Close upstream, check cleanup.
  fake_upstreams_[0].reset();

  ASSERT_TRUE(response_decoder->waitForReset());
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("http.config_test.downstream_rq_active", 0);
  test_server_->waitForGaugeGe("cluster.cluster_0.upstream_cx_tx_bytes_buffered", 0);
}

} // namespace Envoy

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class BandwidthShareIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public HttpIntegrationTest,
                                      public testing::Test {
public:
  BandwidthShareIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initializeFilter(const std::string& config) {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->mutable_per_connection_buffer_limit_bytes()->set_value(4096); // Set to 4KB
    });
    config_helper_.prependFilter(config);
    initialize();
  }

  std::string disabledFilter() {
    // If neither request_bucket_id nor response_bucket_id is set the filter is disabled.
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
  )";
  }
  std::string twoWay1kFilter() {
    return R"(
    name: bwshare
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.bandwidth_share.v3.BandwidthShare
      request_limit: {bucket_id: "bucket1", kbps: {default_value: 1, runtime_key: "a"}}
      response_limit: {bucket_id: "bucket2", kbps: {default_value: 1, runtime_key: "a"}}
  )";
  }
  uint64_t counterValue(absl::string_view c) { return test_server_->counter(std::string{c}); }
  Http::TestRequestHeaderMapImpl request_headers_post_{
      {":method", "POST"}, {":authority", "test"}, {":path", "/test"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"}};

  Http::TestRequestTrailerMapImpl any_request_trailers_{{"x-foo", "foo"}};
  Http::TestResponseTrailerMapImpl any_response_trailers_{{"x-bar", "bar"}};
};

TEST_F(BandwidthShareIntegrationTest, DisabledJustPassesThrough) {
  initializeFilter(disabledFilter());
  auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [request_encoder, response_decoder] = codec_client->startRequest(request_headers_post_);
  codec_client->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client->sendTrailers(request_encoder, any_request_trailers_);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  upstream_request_->encodeData(2048, false);
  upstream_request_->encodeTrailers(any_response_trailers_);
  ASSERT_TRUE(response_decoder->waitForEndStream());

  // TODO: make the metrics the right ones
  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 0);
}

TEST_F(BandwidthShareIntegrationTest, LimitCausesPausesBothWays) {
  initializeFilter(twoWay1kFilter());
  auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [request_encoder, response_decoder] = codec_client->startRequest(request_headers_post_);
  codec_client->sendData(request_encoder, std::string(2048, 'a'), false);
  codec_client->sendTrailers(request_encoder, any_request_trailers_);
  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(response_headers_, false);
  upstream_request_->encodeData(2048, false);
  upstream_request_->encodeTrailers(any_response_trailers_);
  ASSERT_TRUE(response_decoder->waitForEndStream());

  // TODO: make the metrics the right ones
  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 0);
}

} // namespace
} // namespace Envoy

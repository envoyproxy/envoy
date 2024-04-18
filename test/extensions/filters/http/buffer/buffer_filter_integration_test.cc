#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using BufferIntegrationTest = UpstreamDownstreamIntegrationTest;

INSTANTIATE_TEST_SUITE_P(
    Protocols, BufferIntegrationTest,
    testing::ValuesIn(UpstreamDownstreamIntegrationTest::getDefaultTestParams()),
    UpstreamDownstreamIntegrationTest::testParamsToString);

TEST_P(BufferIntegrationTest, RouterNotFoundBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  testRouterNotFoundWithBody();
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  if (upstreamProtocol() == Http::CodecType::HTTP3 ||
      downstreamProtocol() == Http::CodecType::HTTP3) {
    // TODO(#26236) - Fix test flakiness over HTTP/3.
    return;
  }
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(4 * 1024 * 1024, 4 * 1024 * 1024, false);
}

TEST_P(BufferIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(BufferIntegrationTest, RouterRequestPopulateContentLength) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/shelf"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "123", false);
  codec_client_->sendData(*request_encoder_, "456", false);
  codec_client_->sendData(*request_encoder_, "789", true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  auto* content_length = upstream_request_->headers().ContentLength();
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "9");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BufferIntegrationTest, RouterRequestPopulateContentLengthOnTrailers) {
  config_helper_.prependFilter(ConfigHelper::defaultBufferFilter(), testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/shelf"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "0123", false);
  codec_client_->sendData(*request_encoder_, "456", false);
  codec_client_->sendData(*request_encoder_, "789", false);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  auto* content_length = upstream_request_->headers().ContentLength();
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "10");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BufferIntegrationTest, RouterRequestBufferLimitExceeded) {
  // Make sure the connection isn't closed during request upload.
  // Without a large drain-close it's possible that the local reply will be sent
  // during request upload, and continued upload will result in TCP reset before
  // the response is read.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(2000 * 1000); });
  config_helper_.prependFilter(ConfigHelper::smallBufferFilter(), testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

ConfigHelper::HttpModifierFunction overrideConfig(const std::string& json_config) {
  envoy::extensions::filters::http::buffer::v3::BufferPerRoute buffer_per_route;
  TestUtility::loadFromJson(json_config, buffer_per_route);

  return
      [buffer_per_route](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              cfg) {
        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_typed_per_filter_config();

        (*config)["buffer"].PackFrom(buffer_per_route);
      };
}

TEST_P(BufferIntegrationTest, RouteDisabled) {
  if (!testing_downstream_filter_) {
    return;
  }
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"disabled": true})EOF");
  config_helper_.addConfigModifier(mod);
  config_helper_.prependFilter(ConfigHelper::smallBufferFilter(), testing_downstream_filter_);
  config_helper_.setBufferLimits(1024, 1024);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"}},
      1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BufferIntegrationTest, RouteOverride) {
  if (!testing_downstream_filter_) {
    return;
  }
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"buffer": {
    "max_request_bytes": 5242880
  }})EOF");
  config_helper_.addConfigModifier(mod);
  config_helper_.prependFilter(ConfigHelper::smallBufferFilter(), testing_downstream_filter_);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"}},
      1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy

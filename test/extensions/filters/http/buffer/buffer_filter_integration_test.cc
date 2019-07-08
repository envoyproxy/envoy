#include "common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using BufferIntegrationTest = HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, BufferIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(BufferIntegrationTest, RouterNotFoundBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterNotFoundWithBody();
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(4 * 1024 * 1024, 4 * 1024 * 1024, false);
}

TEST_P(BufferIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(BufferIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(BufferIntegrationTest, RouterRequestBufferLimitExceeded) {
  config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024 * 65);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().Status()->value().getStringView());
}

ConfigHelper::HttpModifierFunction overrideConfig(const std::string& json_config) {
  ProtobufWkt::Struct pfc;
  TestUtility::loadFromJson(json_config, pfc);

  return
      [pfc](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& cfg) {
        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_per_filter_config();

        (*config)["envoy.buffer"] = pfc;
      };
}

TEST_P(BufferIntegrationTest, RouteDisabled) {
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"disabled": true})EOF");
  config_helper_.addConfigModifier(mod);
  config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);
  config_helper_.setBufferLimits(1024, 1024);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"}},
                                         1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(BufferIntegrationTest, RouteOverride) {
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"buffer": {
    "max_request_bytes": 5242880
  }})EOF");
  config_helper_.addConfigModifier(mod);
  config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"}},
                                         1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

} // namespace
} // namespace Envoy

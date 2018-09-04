#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class BufferIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConfigHelper::HttpModifierFunction overrideConfig(const std::string& json_config) {
    ProtobufWkt::Struct pfc;
    RELEASE_ASSERT(Protobuf::util::JsonStringToMessage(json_config, &pfc).ok(), "");

    return [pfc](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                     cfg) {
      auto* config = cfg.mutable_route_config()
                         ->mutable_virtual_hosts()
                         ->Mutable(0)
                         ->mutable_per_filter_config();

      (*config)["envoy.buffer"] = pfc;
    };
  }

  ConfigHelper::HttpModifierFunction
  overrideConfigBufferTimeout(std::chrono::seconds max_request_time) {
    // {{ and }} are escaped braces in fmt
    std::string config = fmt::format(R"EOF({{"buffer": {{
      "max_request_time": {{"seconds": {}}}
    }}}})EOF",
                                     max_request_time.count());

    return overrideConfig(config);
  }

  void setupRequestTimeoutTest(const char* method = "GET") {
    initialize();

    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", method},
                                                            {":path", "/test/long/url"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"}});
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
  }
};

INSTANTIATE_TEST_CASE_P(Protocols, BufferIntegrationTest,
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
  testRouterHeaderOnlyRequestAndResponse(true);
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
  EXPECT_STREQ("413", response->headers().Status()->value().c_str());
}

TEST_P(BufferIntegrationTest, RouteDisabled) {
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"disabled": true})EOF");
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
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(BufferIntegrationTest, RouteOverride) {
  ConfigHelper::HttpModifierFunction mod = overrideConfig(R"EOF({"buffer": {
    "max_request_bytes": 5242880,
    "max_request_time": {"seconds": 120}
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
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(BufferIntegrationTest, RequestPathTimesOutInBuffer) {
  std::chrono::seconds buffer_timeout = std::chrono::seconds(1);
  std::chrono::milliseconds test_connection_initiation_timeout = std::chrono::milliseconds(2500);

  ConfigHelper::HttpModifierFunction mod = overrideConfigBufferTimeout(buffer_timeout);
  config_helper_.addConfigModifier(mod);
  config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);
  setupRequestTimeoutTest();

  AssertionResult result = fake_upstreams_[0]->waitForHttpConnection(
      *dispatcher_, fake_upstream_connection_, test_connection_initiation_timeout);

  EXPECT_FALSE(result);
  // TODO Check stats increments
}

// TEST_P(BufferIntegrationTest, RequestPathTimesntOutInBuffer) {
//   std::chrono::seconds buffer_timeout = std::chrono::seconds(1); // Greater than connectiom
//   timeout std::chrono::milliseconds test_connection_initiation_timeout =
//   std::chrono::milliseconds(2500);
//
//   ConfigHelper::HttpModifierFunction mod = overrideConfigBufferTimeout(buffer_timeout);
//   config_helper_.addConfigModifier(mod);
//   config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);
//   AssertionResult result = runRequestTimeoutTest(test_connection_initiation_timeout);
//
//   // TODO assert this will segfault
//   // AssertionResult result =
//   // fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
//   // EXPECT_FALSE(result);
//   // TODO Check stats increments
// }

} // namespace
} // namespace Envoy

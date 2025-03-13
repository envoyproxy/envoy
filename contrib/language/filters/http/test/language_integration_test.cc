#include "test/integration/http_integration.h"

namespace Envoy {
class HttpFilterLanguageIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  HttpFilterLanguageIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void TearDown() override {
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

  void initializeConfig(std::string default_language, std::string supported_languages) {
    constexpr absl::string_view yaml = R"EOF(
name: envoy.filters.http.language
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.language.v3alpha.Language
  default_language: {}
  supported_languages: {}
)EOF";
    config_helper_.prependFilter(fmt::format(yaml, default_language, supported_languages));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpFilterLanguageIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HttpFilterLanguageIntegrationTest, DefaultLanguageFallback) {
  initializeConfig("en", "[fr]");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_FALSE(
      upstream_request_->headers().get(Envoy::Http::LowerCaseString{"x-language"}).empty());
  EXPECT_EQ("en", upstream_request_->headers()
                      .get(Http::LowerCaseString{"x-language"})[0]
                      ->value()
                      .getStringView());

  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(HttpFilterLanguageIntegrationTest, AcceptLanguageHeader) {
  initializeConfig("en", "[en, en-uk, de, dk, es, fr, zh, zh-tw]");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"accept-language", "fr-CH,fr;q=0.9,en;q=0.8,de;q=0.7,*;q=0.5"}});

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_FALSE(
      upstream_request_->headers().get(Envoy::Http::LowerCaseString{"x-language"}).empty());
  EXPECT_EQ("fr", upstream_request_->headers()
                      .get(Http::LowerCaseString{"x-language"})[0]
                      ->value()
                      .getStringView());

  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(HttpFilterLanguageIntegrationTest, InvalidAcceptLanguageHeader) {
  initializeConfig("en", "[en, en-uk, de, dk, es, fr, zh, zh-tw]");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept-language", "foobar;;;;0000.20;0-"}});

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_FALSE(
      upstream_request_->headers().get(Envoy::Http::LowerCaseString{"x-language"}).empty());
  EXPECT_EQ("en", upstream_request_->headers()
                      .get(Http::LowerCaseString{"x-language"})[0]
                      ->value()
                      .getStringView());

  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}
} // namespace Envoy

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

class LocalReplyIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }

  void setLocalReplyConfig(const std::string& yaml) {
    envoy::config::filter::network::http_connection_manager::v2::LocalReplyConfig
        local_reply_config;
    TestUtility::loadFromYaml(yaml, local_reply_config);
    config_helper_.setLocalReply(local_reply_config);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, LocalReplyIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(LocalReplyIntegrationTest, MapStatusCodeAndFormatToJson) {
  const std::string yaml = R"EOF(
mapper:
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value
    rewriter:
      status_code: 550
format:
  json_format:
    level: TRACE
    user_agent: "%REQ(USER-AGENT)%"
    response_body: "%RESP_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"level", "TRACE"},
      {"user_agent", "-"},
      {"response_body", "upstream connect error or disconnect/reset before headers. reset reason: "
                        "connection termination"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("149", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("550", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  TestUtility::verifyJsonOutput(response->body(), expected_json_map);
}

// Should map to first matching filter in this case it will be 2nd filter
TEST_P(LocalReplyIntegrationTest, MapStatusCodeAndFormatToJsonForFirstMatchingFilter) {
  const std::string yaml = R"EOF(
mapper:
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-1
    rewriter:
      status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value
    rewriter:
      status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value
    rewriter:
      status_code: 552
format:
  json_format:
    level: TRACE
    response_flags: "%RESPONSE_FLAGS%"
    response_body: "%RESP_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"level", "TRACE"},
      {"response_flags", "UC"},
      {"response_body", "upstream connect error or disconnect/reset before headers. reset reason: "
                        "connection termination"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("154", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("551", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  TestUtility::verifyJsonOutput(response->body(), expected_json_map);
}

TEST_P(LocalReplyIntegrationTest, ShouldNotMatchAnyFilter) {
  const std::string yaml = R"EOF(
mapper:
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-1
    rewriter:
      status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-2
    rewriter:
      status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-3
    rewriter:
      status_code: 552
format:
  json_format:
    level: TRACE
    response_flags: "%RESPONSE_FLAGS%"
    response_body: "%RESP_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"level", "TRACE"},
      {"response_flags", "UC"},
      {"response_body", "upstream connect error or disconnect/reset before headers. reset reason: "
                        "connection termination"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("154", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  TestUtility::verifyJsonOutput(response->body(), expected_json_map);
}

// Should map response code and return response application/text without any changes.
TEST_P(LocalReplyIntegrationTest, ShouldMapResponseCodeAndMapToDefaultTextResponse) {
  const std::string yaml = R"EOF(
mapper:
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-1
    rewriter:
      status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-2
    rewriter:
      status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-3
    rewriter:
      status_code: 552
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("95", response->headers().ContentLength()->value().getStringView());

  EXPECT_EQ("551", response->headers().Status()->value().getStringView());

  EXPECT_EQ(response->body(), "upstream connect error or disconnect/reset before headers. reset "
                              "reason: connection termination");
}

// Should map response code and return response text/plain with custom format.
TEST_P(LocalReplyIntegrationTest, ShouldMapResponseCodeAndMapToCustomTextResponse) {
  const std::string yaml = R"EOF(
mapper:
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-1
    rewriter:
      status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-2
    rewriter:
      status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          exact_match: exact-match-value-3
    rewriter:
      status_code: 552
format:
  string_format: "%RESPONSE_FLAGS% - %RESP_BODY% - custom response"
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());

  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("118", response->headers().ContentLength()->value().getStringView());

  EXPECT_EQ("551", response->headers().Status()->value().getStringView());

  EXPECT_EQ(response->body(), "UC - upstream connect error or disconnect/reset before headers. "
                              "reset reason: connection termination - custom response");
}

// Should return formatted text/plain response.
TEST_P(LocalReplyIntegrationTest, ShouldFormatResponseToCustomString) {
  const std::string yaml = R"EOF(
format:
  string_format: "%RESPONSE_FLAGS% - %RESP_BODY% - custom response"
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());

  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("118", response->headers().ContentLength()->value().getStringView());

  EXPECT_EQ("503", response->headers().Status()->value().getStringView());

  EXPECT_EQ(response->body(), "UC - upstream connect error or disconnect/reset before headers. "
                              "reset reason: connection termination - custom response");
}

// Should return formatted application/json response.
TEST_P(LocalReplyIntegrationTest, ShouldFormatResponseToJson) {
  const std::string yaml = R"EOF(
format:
  json_format:
    level: TRACE
    response_flags: "%RESPONSE_FLAGS%"
    response_body: "%RESP_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"level", "TRACE"},
      {"response_flags", "UC"},
      {"response_body", "upstream connect error or disconnect/reset before headers. reset reason: "
                        "connection termination"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("154", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  TestUtility::verifyJsonOutput(response->body(), expected_json_map);
}

} // namespace Envoy
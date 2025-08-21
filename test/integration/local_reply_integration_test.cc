#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {

class LocalReplyIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }

  void setLocalReplyConfig(const std::string& yaml) {
    envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig
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
mappers:
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value
    status_code: 550
    headers_to_add:
      - header:
          key: foo
          value: bar
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
      - header:
          key: content-type
          value: "application/json-custom"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
body_format:
  json_format:
    level: TRACE
    user_agent: "%REQ(USER-AGENT)%"
    response_body: "%LOCAL_REPLY_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();

  std::string expected_body;
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    expected_body = R"({
      "level": "TRACE",
      "user_agent": null,
      "response_body": "upstream connect error or disconnect/reset before headers. reset reason: connection termination, transport failure reason: QUIC_NO_ERROR|FROM_PEER|Closed by application"
  })";
  } else {
    expected_body = R"({
      "level": "TRACE",
      "user_agent": null,
      "response_body": "upstream connect error or disconnect/reset before headers. reset reason: connection termination"
  })";
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json-custom", response->headers().ContentType()->value().getStringView());
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_EQ("223", response->headers().ContentLength()->value().getStringView());
  } else {
    EXPECT_EQ("150", response->headers().ContentLength()->value().getStringView());
  }
  EXPECT_EQ("550", response->headers().Status()->value().getStringView());
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_EQ(response->headers().getProxyStatusValue(),
              "envoy; error=connection_terminated; "
              "details=\"upstream_reset_before_response_started{connection_termination|QUIC_NO_"
              "ERROR|FROM_PEER|Closed_by_application}; UC\"");
  } else {
    EXPECT_EQ(response->headers().getProxyStatusValue(),
              "envoy; error=connection_terminated; "
              "details=\"upstream_reset_before_response_started{connection_termination}; UC\"");
  }
  EXPECT_EQ("bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
  // Check if returned json is same as expected
  EXPECT_TRUE(TestUtility::jsonStringEqual(response->body(), expected_body));
}

TEST_P(LocalReplyIntegrationTest, EmptyStructFormatter) {
  const std::string yaml = R"EOF(
mappers:
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value
    status_code: 550
body_format:
  json_format:
    level: TRACE
    user_agent: ""
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  const std::string expected_body = R"({
      "level": "TRACE",
      "user_agent": "",
})";

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("34", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("550", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  EXPECT_TRUE(TestUtility::jsonStringEqual(response->body(), expected_body));
}

// For grpc, the error message is in grpc-message header.
// If it is json, the header value is in json format.
TEST_P(LocalReplyIntegrationTest, MapStatusCodeAndFormatToJson4Grpc) {
  const std::string yaml = R"EOF(
body_format:
  json_format:
    code: "%RESPONSE_CODE%"
    message: "%LOCAL_REPLY_BODY%"
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::string expected_grpc_message;

  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    expected_grpc_message = R"({
      "code": 503,
      "message":"upstream connect error or disconnect/reset before headers. reset reason: connection termination, transport failure reason: QUIC_NO_ERROR|FROM_PEER|Closed by application"
})";
  } else {
    expected_grpc_message = R"({
      "code": 503,
      "message":"upstream connect error or disconnect/reset before headers. reset reason: connection termination"
})";
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/package.service/method"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-type", "application/grpc"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/grpc", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("14", response->headers().GrpcStatus()->value().getStringView());
  // Check if grpc-message value is same as expected
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      std::string(response->headers().GrpcMessage()->value().getStringView()),
      expected_grpc_message));
}

// Like MapStatusCodeAndFormatToJson4Grpc, but to non-json format.
// When grpc is plain text, the grpc-message should remains the same and envoy
// should not truncate the trailing '\n' characters.
TEST_P(LocalReplyIntegrationTest, MapStatusCodeAndFormat2Text4Grpc) {
  const std::string yaml = R"EOF(
body_format:
  text_format_source:
    inline_string: "%LOCAL_REPLY_BODY%:%RESPONSE_CODE%:path=%REQ(:path)%\n"
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  // Note: there should be an %0A at the end.
  std::string expected_grpc_message;
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    expected_grpc_message =
        "upstream connect error or disconnect/reset before headers. reset reason:"
        " connection termination, transport failure reason: "
        "QUIC_NO_ERROR|FROM_PEER|Closed by application:503:path=/package.service/method%0A";
  } else {
    expected_grpc_message =
        "upstream connect error or disconnect/reset before headers. reset reason:"
        " connection termination:503:path=/package.service/method%0A";
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/package.service/method"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-type", "application/grpc"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/grpc", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("14", response->headers().GrpcStatus()->value().getStringView());
  // Check if grpc-message value is same as expected
  EXPECT_EQ(std::string(response->headers().GrpcMessage()->value().getStringView()),
            expected_grpc_message);
}

// Matched second filter has code, headers and body rewrite and its format
TEST_P(LocalReplyIntegrationTest, MapStatusCodeAndFormatToJsonForFirstMatchingFilter) {
  const std::string yaml = R"EOF(
mappers:
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-1
    status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value
    status_code: 551
    headers_to_add:
      - header:
          key: foo
          value: bar
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
    body:
      inline_string: "customized body text"
    body_format_override:
      text_format_source:
        inline_string: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value
    status_code: 552
body_format:
  json_format:
    level: TRACE
    response_flags: "%RESPONSE_FLAGS%"
    response_body: "%LOCAL_REPLY_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  const std::string expected_body = "customized body text 551";

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("24", response->headers().ContentLength()->value().getStringView());
  EXPECT_EQ("551", response->headers().Status()->value().getStringView());
  EXPECT_EQ("bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
  // Check if returned json is same as expected
  EXPECT_EQ(response->body(), expected_body);
}

// Not matching any filters.
TEST_P(LocalReplyIntegrationTest, ShouldNotMatchAnyFilter) {
  const std::string yaml = R"EOF(
mappers:
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-1
    status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-2
    status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-3
    status_code: 552
body_format:
  json_format:
    level: TRACE
    response_flags: "%RESPONSE_FLAGS%"
    response_body: "%LOCAL_REPLY_BODY%"
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  std::string expected_body;
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    expected_body = R"({
      "level": "TRACE",
      "response_flags": "UC",
      "response_body": "upstream connect error or disconnect/reset before headers. reset reason: connection termination, transport failure reason: QUIC_NO_ERROR|FROM_PEER|Closed by application"
      })";
  } else {
    expected_body = R"({
      "level": "TRACE",
      "response_flags": "UC",
      "response_body": "upstream connect error or disconnect/reset before headers. reset reason: connection termination"
      })";
  }

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_EQ("227", response->headers().ContentLength()->value().getStringView());
  } else {
    EXPECT_EQ("154", response->headers().ContentLength()->value().getStringView());
  }
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  // Check if returned json is same as expected
  EXPECT_TRUE(TestUtility::jsonStringEqual(response->body(), expected_body));
}

// Use default formatter.
TEST_P(LocalReplyIntegrationTest, ShouldMapResponseCodeAndMapToDefaultTextResponse) {
  const std::string yaml = R"EOF(
mappers:
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-1
    status_code: 550
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-2
    status_code: 551
  - filter:
      header_filter:
        header:
          name: test-header
          string_match:
            exact: exact-match-value-3
    status_code: 552
  )EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_EQ("168", response->headers().ContentLength()->value().getStringView());
  } else {
    EXPECT_EQ("95", response->headers().ContentLength()->value().getStringView());
  }

  EXPECT_EQ("551", response->headers().Status()->value().getStringView());

  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_EQ(response->body(), "upstream connect error or disconnect/reset before headers. reset "
                                "reason: connection termination, transport failure reason: "
                                "QUIC_NO_ERROR|FROM_PEER|Closed by application");
  } else {
    EXPECT_EQ(response->body(), "upstream connect error or disconnect/reset before headers. reset "
                                "reason: connection termination");
  }
}

// Should return formatted text/plain response.
TEST_P(LocalReplyIntegrationTest, ShouldFormatResponseToCustomString) {
  const std::string yaml = R"EOF(
mappers:
- filter:
    status_code_filter:
      comparison:
        op: EQ
        value:
          default_value: 503
          runtime_key: key_b
  status_code: 513
  body:
    inline_string: "customized body text"
body_format:
  text_format_source:
    inline_string: "%RESPONSE_CODE% - %LOCAL_REPLY_BODY%"
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());

  EXPECT_EQ("text/plain", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("26", response->headers().ContentLength()->value().getStringView());

  EXPECT_EQ("513", response->headers().Status()->value().getStringView());

  EXPECT_EQ(response->body(), "513 - customized body text");
}

// Should return formatted text/plain response.
TEST_P(LocalReplyIntegrationTest, ShouldFormatResponseToEmptyBody) {
  const std::string yaml = R"EOF(
mappers:
- filter:
    status_code_filter:
      comparison:
        op: EQ
        value:
          default_value: 503
          runtime_key: key_b
  status_code: 513
  body:
    inline_string: ""
body_format:
  text_format_source:
    inline_string: ""
)EOF";
  setLocalReplyConfig(yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"test-header", "exact-match-value-2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());

  EXPECT_EQ("513", response->headers().Status()->value().getStringView());

  EXPECT_EQ(response->body(), "");
}

} // namespace Envoy
